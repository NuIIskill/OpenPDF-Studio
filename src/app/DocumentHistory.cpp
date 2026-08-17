#include "app/DocumentHistory.hpp"
#include "app/SessionStore.hpp"

#include <QFile>

DocumentHistory::DocumentHistory(QObject *parent)
    : QObject(parent)
{
}

DocumentHistory::~DocumentHistory()
{
    removeSnapshots();
}

void DocumentHistory::record(const Change &c, int undoIndex,
                             const QList<ImageState> &images,
                             const QString &snapshotSource, Snapshot mode)
{
    // A file is about to take over from the one the deferred snapshot is
    // waiting on, so that file has to be secured first — after this record it
    // is no longer what the view reads from, and nothing keeps it alive.
    if (!snapshotSource.isEmpty()) materializeSnapshot();

    // A new change makes the states that were undone unreachable: they were
    // built on a document that no longer follows from this one.
    while (m_entries.size() > m_current + 1) {
        if (m_pendingEntry == m_entries.size() - 1) {
            m_pendingEntry = -1;
            m_pendingSource.clear();
        }
        SessionStore::discardSnapshot(m_entries.last().snapshot);
        m_entries.removeLast();
    }

    Entry e;
    e.time      = QDateTime::currentDateTime();
    e.kind      = c.kind;
    e.page      = c.page;
    e.count     = c.count;
    e.value     = c.value;
    e.text      = c.text;
    e.undoIndex = undoIndex;
    e.images    = images;
    e.base      = m_entries.isEmpty() ? m_baseCounter : m_entries.last().base;
    if (!snapshotSource.isEmpty()) {
        // A different document file — and with it a different undo stack, so
        // this is where one base ends and the next begins. The base changes
        // even when the copy failed: the states before it are simply no longer
        // reachable, and pretending otherwise would restore the wrong file.
        if (mode == Snapshot::Copy) e.snapshot = takeSnapshot(snapshotSource);
        e.base = ++m_baseCounter;
    }

    m_entries.append(std::move(e));
    m_current = static_cast<int>(m_entries.size()) - 1;

    if (!snapshotSource.isEmpty() && mode == Snapshot::Deferred) {
        m_pendingEntry  = m_current;
        m_pendingSource = snapshotSource;
    }
    Q_EMIT changed();
}

void DocumentHistory::materializeSnapshot()
{
    if (m_pendingEntry < 0 || m_pendingEntry >= m_entries.size()) return;

    const QString source = m_pendingSource;
    const int     entry  = m_pendingEntry;
    m_pendingEntry = -1;
    m_pendingSource.clear();

    m_entries[entry].snapshot = takeSnapshot(source);
}

void DocumentHistory::setCurrentIndex(int index)
{
    if (index < -1 || index >= m_entries.size() || index == m_current) return;
    m_current = index;
    Q_EMIT changed();
}

int DocumentHistory::indexForUndoIndex(int undoIndex) const
{
    if (m_entries.isEmpty()) return -1;

    // Only states in the document file on screen can be meant — an undo index
    // says nothing about the states recorded against an earlier file.
    const int base = m_entries[qBound(0, m_current, count() - 1)].base;
    int fallback = -1;
    for (int i = count() - 1; i >= 0; --i) {
        if (m_entries[i].base != base) continue;
        fallback = i;
        if (m_entries[i].undoIndex <= undoIndex) return i;
    }
    return fallback;
}

QString DocumentHistory::baseFileFor(int index) const
{
    if (index < 0 || index >= m_entries.size()) return {};

    const int base = m_entries[index].base;
    for (int i = index; i >= 0 && m_entries[i].base == base; --i)
        if (!m_entries[i].snapshot.isEmpty()) return m_entries[i].snapshot;
    return {};
}

bool DocumentHistory::restoringDropsEdits(int index) const
{
    if (index < 0 || index >= m_entries.size() || m_current < 0) return false;
    // The file has to be swapped back. Everything the session holds — text
    // edits above all — belongs to the file on screen and cannot follow.
    return m_entries[index].base != m_entries[m_current].base;
}

bool DocumentHistory::canRestore(int index) const
{
    if (index < 0 || index >= m_entries.size() || m_current < 0) return false;
    if (m_entries[index].base == m_entries[m_current].base) return true;
    if (baseFileFor(index).isEmpty()) return false;

    // Restoring across files means loading that file again, and a file holds
    // exactly the state it was written in. Text edits recorded on top of it
    // were never written anywhere, so those states cannot be gone back to —
    // images can, because the entry carries them itself.
    return m_entries[index].undoIndex == anchorUndoIndex(index);
}

int DocumentHistory::anchorUndoIndex(int index) const
{
    if (index < 0 || index >= m_entries.size()) return 0;

    const int base = m_entries[index].base;
    for (int i = index; i >= 0 && m_entries[i].base == base; --i)
        if (!m_entries[i].snapshot.isEmpty()) return m_entries[i].undoIndex;
    return 0;
}

void DocumentHistory::clear()
{
    if (m_entries.isEmpty()) return;

    // The kept state may be living in an earlier entry's snapshot, so it takes
    // that file over — dropping the entry that happened to own it must not
    // delete the document the state is made of.
    Entry keep = m_entries.value(qMax(m_current, 0));
    keep.snapshot = baseFileFor(qMax(m_current, 0));

    for (const Entry &e : std::as_const(m_entries))
        if (e.snapshot != keep.snapshot) SessionStore::discardSnapshot(e.snapshot);

    // A deferred snapshot survives only when it is the kept entry's own.
    if (m_pendingEntry == qMax(m_current, 0)) {
        m_pendingEntry = 0;
    } else {
        m_pendingEntry = -1;
        m_pendingSource.clear();
    }

    m_entries.clear();
    m_entries.append(keep);
    m_current = 0;
    Q_EMIT changed();
}

void DocumentHistory::reset()
{
    removeSnapshots();
    m_entries.clear();
    m_current = -1;
    m_pendingEntry = -1;
    m_pendingSource.clear();
    Q_EMIT changed();
}

QString DocumentHistory::takeSnapshot(const QString &sourcePath)
{
    if (sourcePath.isEmpty() || !QFile::exists(sourcePath)) return {};

    const QString dest = SessionStore::newSnapshotFile(sourcePath);
    if (dest.isEmpty()) return {};
    if (!QFile::copy(sourcePath, dest)) return {};
    return dest;
}

void DocumentHistory::removeSnapshots()
{
    for (const Entry &e : std::as_const(m_entries))
        SessionStore::discardSnapshot(e.snapshot);
}
