#include "engine/historymanager/DocumentJournal.hpp"

#include "app/PdfPwStore.hpp"
#include "app/SessionStore.hpp"
#include "engine/document/DocumentSource.hpp"
#include "engine/historymanager/HistoryArchive.hpp"

#include <QFile>
#include <QFileInfo>
#include <QUndoStack>

#ifdef HAVE_PDF_RENDERING
#  include "engine/edit/EditSession.hpp"
#endif

DocumentJournal::DocumentJournal()
    : m_history(std::make_unique<DocumentHistory>())
{
}

DocumentJournal::~DocumentJournal()
{
    SessionStore::discardSnapshot(m_pendingAnchor);
    SessionStore::discard(m_content);
}

void DocumentJournal::attach(DocumentSource *src, QUndoStack *undo, StateSource state)
{
    m_src   = src;
    m_undo  = undo;
    m_state = std::move(state);
    undoStackChanged();
}

QString DocumentJournal::currentFile() const
{
    if (m_workingCopyDirty && m_target.isEmpty()) return {};
    if (!m_target.isEmpty()) return m_target;
    return m_src ? m_src->contentPath() : QString();
}

QString DocumentJournal::displayName() const
{
    const QString file = currentFile();
    return QFileInfo(file.isEmpty() ? m_suggested : file).fileName();
}

void DocumentJournal::opened(const QString &suggestedPath)
{
    contentMoved();
    m_target.clear();
    m_suggested        = suggestedPath;
    m_workingCopyDirty = false;
    m_savedSideRevision = m_sideRevision;
}

void DocumentJournal::noteDocumentOpened(const DocumentHistory::Change &change)
{
    if (m_restoring) return;

    if (change.kind != DocumentHistory::Kind::Opened) {
        if (!m_pendingAnchor.isEmpty()) {
            m_history->anchorCurrent(m_pendingAnchor, m_pendingAnchorState);
            m_pendingAnchor.clear();
        }
        m_history->record(change, m_undo->index(), m_state(), m_src->contentPath());
        return;
    }

    m_history->reset();
    DocumentHistory::Change opened;
    opened.kind  = DocumentHistory::Kind::Opened;
    opened.count = m_src->pageCount();
    opened.text  = displayName();

    m_history->record(opened, m_undo->index(), m_state(), m_src->contentPath(),
                      DocumentHistory::Snapshot::Deferred);
}

void DocumentJournal::openedAsWorkingCopy(const QString &targetPath)
{
    if (!targetPath.isEmpty() && targetPath == m_src->contentPath()) return;
    m_target           = targetPath;
    m_workingCopyDirty = true;
}

void DocumentJournal::contentMoved()
{
    const QString content = m_src ? m_src->contentPath() : QString();
    if (content == m_content) return;
    SessionStore::discard(m_content);
    m_content = content;
}

void DocumentJournal::closed()
{
    SessionStore::discard(m_content);
    m_content.clear();
    m_target.clear();
    m_suggested.clear();
    m_workingCopyDirty  = false;
    m_savedSideRevision = m_sideRevision;
    m_history->reset();
}

void DocumentJournal::editsDiscarded()
{
#ifdef HAVE_PDF_RENDERING
    if (m_session) m_savedImageRevision = m_session->imageRevision();
#endif
}

void DocumentJournal::recordChange(const DocumentHistory::Change &c,
                                   const QString &snapshotSource)
{
    undoStackChanged();
    if (m_restoring || m_src->contentPath().isEmpty()) return;
    m_history->record(c, m_undo->index(), m_state(), snapshotSource);
}

void DocumentJournal::recordSideChange(const DocumentHistory::Change &c)
{
    if (m_restoring) return;
    ++m_sideRevision;
    recordChange(c);
}

void DocumentJournal::undoStackChanged()
{
    if (m_undo) m_history->setUndoDepth(m_undo->count());
}

void DocumentJournal::prepareAnchor(const std::function<bool(const QString &path)> &write)
{
    dropPendingAnchor();
    if (m_restoring || m_history->currentIsAnchored()) return;

    const QString path = SessionStore::newSnapshotFile(m_src->contentPath());
    if (path.isEmpty()) return;
    if (!write(path)) {
        SessionStore::discardSnapshot(path);
        return;
    }

    DocumentHistory::DocumentState state = m_state();
    state.images.clear();
    for (QByteArray &overlay : state.overlays) overlay.clear();
    m_pendingAnchor      = path;
    m_pendingAnchorState = state;
}

void DocumentJournal::dropPendingAnchor()
{
    SessionStore::discardSnapshot(m_pendingAnchor);
    m_pendingAnchor.clear();
    m_pendingAnchorState = {};
}

void DocumentJournal::clearHistory()
{
    m_history->clear();
}

void DocumentJournal::prepareSave()
{
    m_history->materializeSnapshot();
}

void DocumentJournal::recordSaved(const QString &path, const QString &snapshotSource)
{
    DocumentHistory::Change saved;
    saved.kind  = DocumentHistory::Kind::Saved;
    saved.count = m_src->pageCount();
    saved.text  = QFileInfo(path).fileName();
    recordChange(saved, snapshotSource);
}

#ifdef HAVE_PDF_RENDERING
void DocumentJournal::markSaved(const QString &path)
{
    m_undo->setClean();
    m_savedImageRevision = m_session->imageRevision();
    m_savedSideRevision  = m_sideRevision;
    m_workingCopyDirty   = false;
    m_suggested.clear();

    m_target = (QFileInfo(path).absoluteFilePath()
                    == QFileInfo(m_src->contentPath()).absoluteFilePath())
                   ? QString() : path;

    recordSaved(path, QString());
}
#else
void DocumentJournal::markSaved(const QString &path)
{
    Q_UNUSED(path)
}
#endif

void DocumentJournal::savedOverBase(const QString &path, bool reopened)
{
    if (reopened) {
        contentMoved();
        m_target.clear();
        m_workingCopyDirty = false;
    }
    m_savedSideRevision = m_sideRevision;
    recordSaved(path, m_src->contentPath());
}

bool DocumentJournal::hasUnsavedEdits() const
{
    if (m_workingCopyDirty) return true;
    if (m_sideRevision != m_savedSideRevision) return true;
#ifdef HAVE_PDF_RENDERING
    if (!m_session) return false;

    return !m_undo->isClean()
        || m_session->imageRevision() != m_savedImageRevision;
#else
    return false;
#endif
}

QString DocumentJournal::copyToWorkingFile(const QString &source, const QString &like) const
{
    if (source.isEmpty()) return {};

    const QString work = SessionStore::newWorkingFile(like.isEmpty() ? source : like);
    if (work.isEmpty()) return {};
    QFile::remove(work);
    if (!QFile::copy(source, work)) {
        SessionStore::discard(work);
        return {};
    }
    PdfPwStore::set(work, PdfPwStore::get(like.isEmpty() ? source : like));
    return work;
}

void DocumentJournal::discardCopy(const QString &path) const
{
    if (path != m_content) SessionStore::discard(path);
}

DocumentJournal::RestorePlan DocumentJournal::planRestore(int index)
{
    RestorePlan plan;
    if (!m_history->canRestore(index)) return plan;

    const DocumentHistory::Entry entry = m_history->entries().value(index);
    plan.index     = index;
    plan.undoIndex = entry.undoIndex;
    plan.state     = entry.state;
    plan.before    = m_state();

    if (m_history->restoringDropsEdits(index)) {
        plan.target     = currentFile();
        plan.reopenFile = copyToWorkingFile(m_history->baseFileFor(index), plan.target);
        if (plan.reopenFile.isEmpty()) return plan;
    }
    plan.ok = true;
    return plan;
}

void DocumentJournal::finishRestore(const RestorePlan &plan, bool ok)
{
    if (!ok) {
        discardCopy(plan.reopenFile);
        return;
    }

    if (!plan.reopenFile.isEmpty()) m_workingCopyDirty = true;
    if (plan.state.bookmarks != plan.before.bookmarks
            || plan.state.overlays != plan.before.overlays)
        ++m_sideRevision;
    m_history->setCurrentIndex(plan.index);
}

bool DocumentJournal::writeArchive(const QString &path)
{
    m_history->materializeSnapshot();
    return HistoryArchive::write(m_history->entries(), m_history->currentIndex(), path);
}

bool DocumentJournal::adoptArchive(const QString &path)
{
    HistoryArchive::Timeline timeline;
    if (!HistoryArchive::read(path, &timeline)) return false;

    m_history->adopt(std::move(timeline.entries), timeline.current,
                     m_src->contentPath(), m_state(), m_undo->index());
    undoStackChanged();
    SessionStore::discardSnapshot(path);
    return true;
}
