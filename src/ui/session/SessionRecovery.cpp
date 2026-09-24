#include "ui/session/SessionRecovery.hpp"

#include "engine/historymanager/DocumentHistory.hpp"
#include "engine/historymanager/HistoryArchive.hpp"
#include "ui/DocumentView.hpp"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTimer>

namespace {

constexpr int kTickMs     = 5000;
constexpr int kSettleMs   = 5000;
constexpr int kMinApartMs = 30000;

QString displayName(const SessionStore::OpenDocument &doc)
{
    if (!doc.target.isEmpty()) return QFileInfo(doc.target).fileName();
    return SessionRecovery::tr("Untitled");
}

}

SessionRecovery::SessionRecovery(QWidget *parent)
    : QObject(parent)
    , m_parent(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, &SessionRecovery::tick);
}

SessionRecovery::~SessionRecovery()
{
    finish();
}

QList<SessionStore::OpenDocument> SessionRecovery::offerAbandonedDocuments()
{
    const QList<SessionStore::OpenDocument> abandoned =
        SessionStore::takeAbandonedDocuments();
    if (abandoned.isEmpty()) return {};

    QStringList names;
    for (const SessionStore::OpenDocument &doc : abandoned) {
        const QString name = displayName(doc);
        names.append(doc.dirty ? tr("%1 (unsaved changes)").arg(name) : name);
    }

    QMessageBox box(m_parent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Restore Session"));
    box.setText(tr("OpenPDF Studio did not shut down properly last time."));
    box.setInformativeText(tr("Restore the documents that were open?"));
    box.setDetailedText(names.join(QLatin1Char('\n')));
    QPushButton *restore = box.addButton(tr("Restore"), QMessageBox::AcceptRole);
    box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.setDefaultButton(restore);
    box.setEscapeButton(restore);
    box.exec();

    if (box.clickedButton() != restore) {
        for (const SessionStore::OpenDocument &doc : abandoned) discard(doc);
        return {};
    }
    return abandoned;
}

void SessionRecovery::discard(const SessionStore::OpenDocument &doc)
{
    SessionStore::discard(doc.content);
    HistoryArchive::discard(doc.history);
}

void SessionRecovery::begin()
{
    if (m_active) return;
    if (!SessionStore::beginSession()) {
        qWarning() << "SessionRecovery: no session record, a crash cannot be recovered";
        return;
    }
    m_active = true;
    syncManifest();
    m_timer->start();
}

void SessionRecovery::watch(DocumentView *view)
{
    if (!view || m_views.contains(view)) return;
    m_views.append(view);

    connect(view->history(), &DocumentHistory::changed, this,
            [this, view] { noteChange(view); });
    connect(view, &DocumentView::fileOpened, this,
            [this, view] { dropCopy(view); });
    connect(view, &QObject::destroyed, this, [this, view] {
        m_views.removeAll(view);
        dropCopy(view);
    });
}

void SessionRecovery::forget(DocumentView *view)
{
    if (!view) return;
    dropCopy(view);
    m_views.removeAll(view);
    syncManifest();
}

void SessionRecovery::finish()
{
    m_timer->stop();
    for (auto it = m_copies.cbegin(); it != m_copies.cend(); ++it) {
        SessionStore::discard(it.value().path);
        SessionStore::discardSnapshot(it.value().archive);
    }
    m_copies.clear();
    if (m_active) SessionStore::endSession();
    m_active = false;
}

void SessionRecovery::noteChange(DocumentView *view)
{
    Copy &copy = m_copies[view];
    copy.stale     = true;
    copy.changedAt = QDateTime::currentMSecsSinceEpoch();
}

void SessionRecovery::dropCopy(DocumentView *view)
{
    const auto it = m_copies.constFind(view);
    if (it == m_copies.cend()) return;
    SessionStore::discard(it.value().path);
    SessionStore::discardSnapshot(it.value().archive);
    m_copies.erase(it);
}

void SessionRecovery::tick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (DocumentView *view : std::as_const(m_views)) {
        const bool dirty    = view->hasUnsavedEdits();
        const bool timeline = view->history()->count() > 1;
        if (view->pageCount() <= 0 || (!dirty && !timeline)) {
            dropCopy(view);
            continue;
        }

        auto it = m_copies.find(view);
        if (it == m_copies.end())
            it = m_copies.insert(view, Copy { QString(), QString(), true, now, 0 });
        if (!it->stale) continue;
        if (now - it->changedAt < kSettleMs) continue;
        if (it->writtenAt != 0 && now - it->writtenAt < kMinApartMs) continue;

        Copy &copy = *it;
        copy.writtenAt = now;
        if (dirty) {
            if (!writeContent(view, copy)) continue;
        } else {
            SessionStore::discard(copy.path);
            copy.path.clear();
        }
        if (timeline) {
            if (!writeArchive(view, copy)) continue;
        } else {
            SessionStore::discardSnapshot(copy.archive);
            copy.archive.clear();
        }
        copy.stale = false;
    }

    syncManifest();
}

bool SessionRecovery::writeContent(DocumentView *view, Copy &copy)
{
    const bool fresh = copy.path.isEmpty();
    const QString path = fresh
        ? SessionStore::newWorkingFile(view->currentFile().isEmpty() ? view->contentFile()
                                                                     : view->currentFile())
        : copy.path;
    if (path.isEmpty()) return false;

    if (!view->writeRecoveryCopy(path)) {
        qWarning() << "SessionRecovery: could not write" << path;
        if (fresh) SessionStore::discard(path);
        return false;
    }
    copy.path = path;
    return true;
}

bool SessionRecovery::writeArchive(DocumentView *view, Copy &copy)
{
    const bool fresh = copy.archive.isEmpty();
    const QString path = fresh ? SessionStore::newArchiveFile(view->contentFile()) : copy.archive;
    if (path.isEmpty()) return false;

    if (!view->writeTimeline(path)) {
        qWarning() << "SessionRecovery: could not write" << path;
        if (fresh) SessionStore::discardSnapshot(path);
        return false;
    }
    copy.archive = path;
    return true;
}

void SessionRecovery::syncManifest()
{
    if (!m_active) return;

    QList<SessionStore::OpenDocument> documents;
    for (DocumentView *view : std::as_const(m_views)) {
        if (view->pageCount() <= 0) continue;

        SessionStore::OpenDocument doc;
        doc.target  = view->currentFile();
        doc.content = m_copies.value(view).path;
        doc.history = m_copies.value(view).archive;
        doc.page    = view->currentPage();
        doc.dirty   = view->hasUnsavedEdits();
        if (doc.target.isEmpty() && doc.content.isEmpty()) continue;
        documents.append(doc);
    }
    SessionStore::updateSession(documents);
}
