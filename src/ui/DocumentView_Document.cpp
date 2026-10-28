

#include "ui/DocumentView.hpp"

#include "app/PdfPwStore.hpp"
#include "engine/document/BookmarkWriter.hpp"
#include "engine/document/DocumentSource.hpp"
#include "engine/edit/InkMetrics.hpp"
#include "engine/sign/SignatureRemover.hpp"
#include "app/SafeWrite.hpp"
#include "app/SessionStore.hpp"
#include "ui/view/AnnotationLoader.hpp"
#include "ui/view/DocumentInfoLoader.hpp"
#include "ui/view/ProtectionLayer.hpp"
#include "ui/view/ImageAnnotation.hpp"
#include "ui/view/ImageAnnotationLayer.hpp"
#include "ui/view/LinkAnnotationLayer.hpp"
#include "ui/notes/NoteLayer.hpp"
#include "ui/draw/DrawingLayer.hpp"
#include "ui/view/FindController.hpp"
#include "ui/view/PageOverlay.hpp"
#include "ui/view/HoverHighlight.hpp"
#include "ui/view/LoadingSpinner.hpp"
#include "ui/view/DigitalSignatureLayer.hpp"
#include "ui/view/SignaturePlacement.hpp"
#include "ui/view/PageLayoutEngine.hpp"
#include "ui/view/ZoomController.hpp"
#include "ui/view/TextSelectionController.hpp"
#include "ui/widgets/PasswordDialog.hpp"

#include <QFile>
#include <QEventLoop>
#include <QFileInfo>

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <cstring>
#endif

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QRubberBand>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QCursor>
#include <QScrollBar>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPalette>
#include <QPainter>
#include <QApplication>
#include <QClipboard>
#include <QTextEdit>
#include <QStyle>
#include <QTimer>
#include <QDebug>
#include <QMap>
#include <QFileDialog>
#include <QFontMetrics>
#include <QMenu>
#include <QRegularExpression>
#include <QUndoCommand>
#include <QUndoStack>
#include <QStringList>
#include <QKeyEvent>
#include <algorithm>
#include <functional>
#include <limits>
#include <optional>

namespace {

bool editableBookmarks(const QList<PdfBookmark> &bookmarks)
{
    for (const PdfBookmark &bookmark : bookmarks)
        if (!bookmark.supported || !editableBookmarks(bookmark.children))
            return false;
    return true;
}

}

void DocumentView::clearDocument()
{
    cancelCurrentEdit();
    m_signaturePlacement->cancel();
    m_selection->clear();
    m_find->documentChanged();

    if (m_viewMode == ViewMode::Grid) {
        m_layoutEngine->clearGrid();
        m_gridCanvas->hide();
        m_gridCanvas->setMinimumHeight(0);
        takeWidget();
        setWidget(m_canvas);
        m_canvas->show();
        m_viewMode = ViewMode::Single;
        Q_EMIT viewModeChanged(ViewMode::Single);
    }
#ifdef HAVE_PDF_RENDERING
    m_hover->hide();
    discardEditHistory();
    m_edit.clearOcrCache();

    m_src->close();
#endif
    m_src->setContentPath(QString());
    m_info->clear();
    m_journal.closed();
    m_bookmarks.clear();
    m_bookmarksDirty = false;
    Q_EMIT bookmarkDataChanged();

#ifdef HAVE_PDF_RENDERING
    m_imageLayer->setSource(m_src->renderer(), m_session);
#endif
    m_src->setPageCount(0);
    m_lastReportedPage = -1;

    m_layoutEngine->clearPages();

    m_imageLayer->clear();
    m_annotations->start(0);
    m_drawingLayer->clear();
    for (PageOverlay *overlay : std::as_const(m_overlays))
        overlay->setDocument(QString());

    m_startPage->show();
}

QString DocumentView::displayName() const
{
    return m_journal.displayName();
}

bool DocumentView::openFile(const QString &path, const QString &suggestedPath)
{
    if (!openContent(path, suggestedPath, {})) return false;
    m_protectionLayer->unlock(QString());
    return true;
}

void DocumentView::setProtection(const PdfProtection &protection)
{
    m_protectionLayer->set(protection);
}

const std::optional<PdfProtection> &DocumentView::pendingProtection() const
{
    return m_protectionLayer->pending();
}

DocumentInfo::Permissions DocumentView::permissions() const
{
#ifdef HAVE_PDF_RENDERING
    if (m_src->backend() && m_protectionLayer->ownerPassword().isEmpty())
        return m_src->backend()->permissions();
#endif
    return {};
}

bool DocumentView::unlockPermissions(const QString &ownerPassword)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_src->backend()
            || !m_src->backend()->isOwnerPassword(m_src->contentPath(), ownerPassword))
        return false;
    m_protectionLayer->unlock(ownerPassword);
    return true;
#else
    Q_UNUSED(ownerPassword)
    return false;
#endif
}

QString DocumentView::permissionsPassword() const
{
    return m_protectionLayer->ownerPassword();
}

bool DocumentView::openContent(const QString &path, const QString &suggestedPath,
                               const DocumentHistory::Change &change)
{
    if (path.isEmpty()) return false;
    cancelCurrentEdit();

    if (m_viewMode == ViewMode::Grid)
        setViewMode(ViewMode::Single);

#ifdef HAVE_PDF_RENDERING
    m_spinner->showNow();
    if (!m_src->open(path, askPassword())) {
        m_spinner->setBusy(m_layoutEngine->busy());
        return false;
    }

    discardEditHistory();

    m_edit.clearOcrCache();
    m_journal.opened(suggestedPath);
    m_bookmarks      = m_src->backend()->bookmarks();
    m_bookmarksDirty = false;
    Q_EMIT bookmarkDataChanged();

    m_imageLayer->setSource(m_src->renderer(), m_session);
    for (PageOverlay *overlay : std::as_const(m_overlays))
        overlay->setDocument(m_src->contentPath());
    m_layoutEngine->setPageCount(m_src->pageCount());
    resetContentProvider();
    m_startPage->hide();
    m_layoutEngine->buildPages();
    m_find->documentChanged();
    m_annotations->start(m_src->pageCount());

    QMetaObject::invokeMethod(this, [this]() { syncVisibleRect(); },
                              Qt::QueuedConnection);
    m_journal.noteDocumentOpened(change);
    m_info->load(m_src->contentPath(), PdfPwStore::forQpdf(m_src->contentPath()));
    Q_EMIT fileOpened(m_src->contentPath(), m_src->pageCount());
    m_lastReportedPage = 0;
    Q_EMIT pageChanged(1, m_src->pageCount());
    return true;

#else
    m_src->setContentPath(path);
    m_journal.opened(suggestedPath);
    m_bookmarks.clear();
    m_bookmarksDirty = false;
    Q_EMIT bookmarkDataChanged();
    m_src->setPageCount(1);
    m_find->documentChanged();
    m_startPage->show();
    retranslateUi();
    m_journal.noteDocumentOpened(change);
    Q_EMIT fileOpened(m_src->contentPath(), m_src->pageCount());
    m_lastReportedPage = 0;
    Q_EMIT pageChanged(1, m_src->pageCount());
    return true;
#endif
}

bool DocumentView::openWorkingCopy(const QString &contentPath,
                                   const QString &targetPath,
                                   const DocumentHistory::Change &change,
                                   const QString &suggestedPath)
{
    if (!targetPath.isEmpty() && !PdfPwStore::has(contentPath))
        PdfPwStore::set(contentPath, PdfPwStore::get(targetPath));

    // A working copy of the same document keeps the name an unsaved import was
    // given, or its tab would lose its label.
    const QString suggested = suggestedPath.isEmpty()
                                  && change.kind != DocumentHistory::Kind::Opened
                              ? suggestedSavePath() : suggestedPath;

    if (change.kind != DocumentHistory::Kind::Opened && pageCount() > 0)
        m_journal.prepareAnchor([this](const QString &path) { return writeRecoveryCopy(path); });
    if (!openContent(contentPath, suggested, change)) {
        m_journal.dropPendingAnchor();
        return false;
    }
    if (targetPath == contentPath) return true;

    m_journal.openedAsWorkingCopy(targetPath);
    Q_EMIT fileOpened(targetPath, m_src->pageCount());
    return true;
}

bool DocumentView::removeSignature(const QString &field)
{
#ifdef HAVE_PDF_RENDERING
    const QString source = m_src->contentPath();
    if (source.isEmpty() || hasUnsavedEdits()) return false;
    const QString work = m_journal.copyToWorkingFile(source, currentFile());
    if (work.isEmpty()) return false;
    if (!SignatureRemover::remove(source, work, field, PdfPwStore::forQpdf(source))) {
        m_journal.discardCopy(work);
        return false;
    }
    PdfPwStore::set(work, PdfPwStore::get(source));

    DocumentHistory::Change change;
    change.kind = DocumentHistory::Kind::OverlayChanged;
    change.text = tr("Signature removed");
    if (!openWorkingCopy(work, currentFile(), change)) {
        m_journal.discardCopy(work);
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool DocumentView::saveToFile(const QString &path)
{
#ifdef HAVE_PDF_RENDERING

    m_journal.prepareSave();
    m_signatureLayer->clearError();
    commitCurrentEdit(m_editorFrame->currentText());

    if (!m_src->backend() || !m_session || m_src->pageCount() <= 0) return false;

    const bool signing = m_signatureLayer->hasPending();
    // Without edits on top of the content its bytes are kept, so the
    // signatures in it stay valid.
    const bool keepBytes = !hasEditsBesideSignatures();
    const std::string password = PdfPwStore::forQpdf(m_src->contentPath());
    const std::optional<PdfProtection> protection = m_protectionLayer->pending();
    const bool protecting = protection.has_value();
    const QString newPassword = protecting && protection->encrypt ? protection->userPassword
                                                                  : QString();
    const bool detached = detachSourceFrom(path);

    const QString staging = keepBytes ? copyContent(path) : stageDocument(path);
    if (staging.isEmpty()) return false;
    if (protecting && !ProtectionWriter::apply(staging, password, *protection)) {
        SafeWrite::discard(staging);
        return false;
    }
    if (signing && m_signatureLayer->signInto(staging) != SignError::None) {
        SafeWrite::discard(staging);
        return false;
    }

    // A signed or newly protected file is final: the view reloads it instead
    // of staying on a working copy that still carries the old encryption.
    if (detached && !signing && !protecting) {
        if (!SafeWrite::commit(staging, path)) return false;
        m_bookmarksDirty = false;
        m_journal.markSaved(path);
        Q_EMIT bookmarkDataChanged();
        m_info->load(path, password);
        return true;
    }

    const QString reopenPath = m_src->contentPath();
    m_src->close();

    if (!SafeWrite::commit(staging, path)) {
        m_src->open(reopenPath, nullptr);
        resetContentProvider();
        return false;
    }

    if (protecting) {
        if (newPassword.isEmpty()) PdfPwStore::forget(path);
        else PdfPwStore::set(path, newPassword);
        m_protectionLayer->unlock(protection->encrypt ? protection->ownerPassword : QString());
    }
    discardEditHistory();
    const bool reopened = m_src->open(path, nullptr);
    if (!reopened) {
        qWarning() << "[SAVE] wrote" << path << "but could not reopen it";
        m_src->open(reopenPath, nullptr);
    }
    resetContentProvider();
    m_annotations->start(m_src->pageCount());

    for (PageOverlay *overlay : std::as_const(m_overlays))
        overlay->setDocument(m_src->contentPath());
    if (m_src->backend()) m_bookmarks = m_src->backend()->bookmarks();
    m_bookmarksDirty = false;
    Q_EMIT bookmarkDataChanged();
    m_layoutEngine->rerenderAll();
    m_journal.savedOverBase(path, reopened);
    m_info->load(path, protecting ? newPassword.toStdString() : password);
    return true;
#else
    Q_UNUSED(path)
    return false;
#endif
}

void DocumentView::setBookmarks(const QList<PdfBookmark> &bookmarks)
{
    if (bookmarks == m_bookmarks) return;
    m_bookmarks = bookmarks;
    m_bookmarksDirty = true;
    m_journal.recordSideChange({ DocumentHistory::Kind::BookmarksChanged });
    Q_EMIT bookmarkDataChanged();
}

bool DocumentView::bookmarkEditingAvailable() const
{
    return m_src->pageCount() > 0 && BookmarkWriter::available()
        && editableBookmarks(m_bookmarks);
}

#ifdef HAVE_PDF_RENDERING
QString DocumentView::stageDocument(const QString &path)
{
    auto *backend = m_src->backend();
    if (!backend || !m_session || m_src->pageCount() <= 0) return {};

    const QString staging = SafeWrite::stagingPath(path);
    if (staging.isEmpty()) return {};

    if (!writeEditsAndWait(staging) || !finishStaging(staging)) {
        SafeWrite::discard(staging);
        return {};
    }
    return staging;
}

bool DocumentView::writeEditsAndWait(const QString &staging)
{
    // The worker writes while this loop keeps painting, so the spinner turns
    // instead of the window freezing; input waits until the file is written.
    bool written = false;
    QEventLoop loop;
    connect(m_src->worker(), &DocumentWorker::copyWritten, &loop,
            [&](const QString &output, bool ok) {
        if (output != staging) return;
        written = ok;
        loop.quit();
    });
    m_spinner->showNow();
    m_src->worker()->writeCopy(staging, std::make_shared<EditSession>(*m_session));
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    m_spinner->setBusy(m_layoutEngine->busy());
    return written;
}

bool DocumentView::finishStaging(const QString &staging)
{
    for (PageOverlay *overlay : std::as_const(m_overlays))
        if (!overlay->writeTo(staging)) return false;

    return !m_bookmarksDirty
        || BookmarkWriter::write(staging, m_bookmarks, PdfPwStore::get(m_src->contentPath()));
}
#endif

bool DocumentView::writeRecoveryCopy(const QString &path)
{
#ifdef HAVE_PDF_RENDERING
    if (path.isEmpty() || path == m_src->contentPath()) return false;

    const bool keepBytes = !hasEditsBesideSignatures() && !m_signatureLayer->hasPending();
    const QString staging = keepBytes ? copyContent(path) : stageDocument(path);
    if (staging.isEmpty()) return false;
    return SafeWrite::commit(staging, path);
#else
    Q_UNUSED(path)
    return false;
#endif
}

void DocumentView::writeRecoveryCopyInBackground(const QString &path,
                                                 const std::function<void(bool)> &done)
{
#ifdef HAVE_PDF_RENDERING
    const bool usable = !path.isEmpty() && path != m_src->contentPath()
                     && m_src->backend() && m_src->pageCount() > 0;
    const QString staging = usable ? SafeWrite::stagingPath(path) : QString();
    if (staging.isEmpty()) {
        done(false);
        return;
    }

    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(m_src->worker(), &DocumentWorker::copyWritten, this,
                          [this, connection, path, staging, done](const QString &output, bool ok) {
        if (output != staging) return;
        disconnect(*connection);
        ok = ok && finishStaging(staging);
        if (!ok) SafeWrite::discard(staging);
        done(ok && SafeWrite::commit(staging, path));
    });
    m_src->worker()->writeCopy(staging, std::make_shared<EditSession>(*m_session));
#else
    Q_UNUSED(path)
    done(false);
#endif
}

#ifdef HAVE_PDF_RENDERING
bool DocumentView::hasEditsBesideSignatures() const
{
    if (m_journal.hasUnsavedSessionEdits() || m_bookmarksDirty) return true;
    // A save that kept the working copy leaves its edits in the session only.
    if (m_session && m_session->hasAnyEdits()) return true;
    if (m_src->backend() && m_src->backend()->bookmarks() != m_bookmarks) return true;
    for (const PageOverlay *overlay : std::as_const(m_overlays))
        if (overlay != m_signatureLayer && !overlay->state().isEmpty()) return true;
    return false;
}

QString DocumentView::copyContent(const QString &path) const
{
    const QString staging = SafeWrite::stagingPath(path);
    if (staging.isEmpty()) return {};
    QFile::remove(staging);
    return QFile::copy(m_src->contentPath(), staging) ? staging : QString();
}

bool DocumentView::detachSourceFrom(const QString &saveTarget)
{
    if (m_src->contentPath().isEmpty() || saveTarget.isEmpty()) return false;

    if (SessionStore::isWorkingFile(m_src->contentPath())) return true;

    if (QFileInfo(m_src->contentPath()).absoluteFilePath()
            != QFileInfo(saveTarget).absoluteFilePath())
        return true;

    const QString original = m_src->contentPath();
    const QString work = m_journal.copyToWorkingFile(original, original);
    if (work.isEmpty()) return false;

    if (!m_src->open(work, nullptr)) {
        m_src->open(original, nullptr);
        m_journal.discardCopy(work);
        return false;
    }

    m_journal.contentMoved();
    resetContentProvider();
    return true;
}

#endif

namespace {

// What the undo stack holds, kept with every history entry so it outlives a crash.
const QString kTextKey     = QStringLiteral("text");
const QString kNotesKey    = QStringLiteral("notes");
const QString kLinksKey    = QStringLiteral("links");
const QString kDrawingsKey = QStringLiteral("drawings");

}

DocumentHistory::DocumentState DocumentView::documentState() const
{
    DocumentHistory::DocumentState state;
    const QList<ImageAnnotationLayer::Placed> placed = m_imageLayer->placedImages();
    state.images.reserve(placed.size());
    for (const auto &p : placed)
        state.images.append({ p.page, p.pdfBounds, p.image });
    state.bookmarks = m_bookmarks;
#ifdef HAVE_PDF_RENDERING
    state.edits.insert(ImageAnnotationLayer::changesKey(), m_imageLayer->changeState());
    state.edits.insert(kTextKey, m_edit.encodedEdits());
    state.edits.insert(kDrawingsKey, m_drawingLayer->encodedState());
#endif
    state.edits.insert(kNotesKey, m_noteLayer->encodedState());
    state.edits.insert(kLinksKey, m_linkLayer->encodedState());
    for (const PageOverlay *overlay : std::as_const(m_overlays)) {
        const QString key = overlay->stateKey();
        if (!key.isEmpty()) state.overlays.insert(key, overlay->state());
    }
    return state;
}

void DocumentView::applyState(const DocumentHistory::DocumentState &state)
{
#ifdef HAVE_PDF_RENDERING
    QList<ImageAnnotationLayer::Placed> images;
    images.reserve(state.images.size());
    for (const DocumentHistory::ImageState &s : state.images)
        images.append({ s.page, s.pdfBounds, s.image });
    m_imageLayer->restoreImages(images);
    m_imageLayer->restoreChanges(state.edits.value(ImageAnnotationLayer::changesKey()));
    // Entries from before these were kept leave them to the undo stack.
    if (state.edits.contains(kTextKey))     m_edit.restoreEncodedEdits(state.edits.value(kTextKey));
    if (state.edits.contains(kDrawingsKey)) m_drawingLayer->restoreEncoded(state.edits.value(kDrawingsKey));
#endif
    if (state.edits.contains(kNotesKey)) m_noteLayer->restoreEncoded(state.edits.value(kNotesKey));
    if (state.edits.contains(kLinksKey)) m_linkLayer->restoreEncoded(state.edits.value(kLinksKey));
    if (state.bookmarks != m_bookmarks) {
        m_bookmarks      = state.bookmarks;
        m_bookmarksDirty = true;
        Q_EMIT bookmarkDataChanged();
    }
    for (PageOverlay *overlay : std::as_const(m_overlays)) {
        const QString key = overlay->stateKey();
        if (!key.isEmpty()) overlay->restoreState(state.overlays.value(key));
    }
}

bool DocumentView::restoreHistoryState(int index)
{
    closeEditorBeforeUndo();
    if (index == m_journal.history()->currentIndex())
        return m_journal.history()->canRestore(index);

    const DocumentJournal::RestorePlan plan = m_journal.planRestore(index);
    if (!plan.ok) return false;

    bool ok = true;
    {
        const DocumentJournal::RestoreGuard guard = m_journal.restoring();
        if (!plan.reopenFile.isEmpty())
            ok = openWorkingCopy(plan.reopenFile, plan.target);
        else
            m_undoStack->setIndex(plan.undoIndex);
        if (ok) applyState(plan.state);
    }
    m_journal.finishRestore(plan, ok);
    return ok;
}

void DocumentView::clearHistory()
{
    m_journal.clearHistory();
}

bool DocumentView::writeTimeline(const QString &path)
{
    return m_journal.writeArchive(path);
}

bool DocumentView::adoptTimeline(const QString &path)
{
    return m_journal.adoptArchive(path);
}

namespace {

/// An undo step rebuilt from the change log, from one kept state to the next.
class ResumedStep : public QUndoCommand
{
public:
    using Apply = std::function<void(const DocumentHistory::DocumentState &)>;

    ResumedStep(Apply apply, DocumentHistory::DocumentState before,
                DocumentHistory::DocumentState after)
        : m_apply(std::move(apply)), m_before(std::move(before)), m_after(std::move(after)) {}

    void undo() override { m_apply(m_before); }
    void redo() override
    {
        if (m_first) { m_first = false; return; }
        m_apply(m_after);
    }

private:
    Apply m_apply;
    DocumentHistory::DocumentState m_before;
    DocumentHistory::DocumentState m_after;
    bool  m_first { true };
};

}

bool DocumentView::resumeTimeline(const QString &archive, const QString &target)
{
#ifdef HAVE_PDF_RENDERING
    std::optional<DocumentJournal::Resumable> timeline = DocumentJournal::readResumable(archive);
    if (!timeline) return false;
    const QString work = m_journal.copyToWorkingFile(timeline->baseFile, target);
    if (work.isEmpty()) return false;
    if (!openWorkingCopy(work, target)) {
        m_journal.discardCopy(work);
        return false;
    }

    // The undo stack is gone with the crash; each index gets back the state
    // its entries kept, so restoring them works as before.
    const QList<DocumentHistory::Entry> &entries = timeline->entries;
    const auto apply = [this](const DocumentHistory::DocumentState &s) { applyState(s); };
    m_undoStack->clear();
    DocumentHistory::DocumentState previous = entries[timeline->anchor].state;
    for (int k = 1; k <= entries[timeline->current].undoIndex; ++k) {
        DocumentHistory::DocumentState next = previous;
        for (int i = timeline->anchor; i <= timeline->current; ++i)
            if (entries[i].undoIndex == k) next = entries[i].state;
        m_undoStack->push(new ResumedStep(apply, previous, next));
        previous = std::move(next);
    }

    const DocumentHistory::DocumentState now = entries[timeline->current].state;
    m_journal.resume(std::move(*timeline), archive);
    const DocumentJournal::RestoreGuard guard = m_journal.restoring();
    applyState(now);
    return true;
#else
    Q_UNUSED(archive) Q_UNUSED(target)
    return false;
#endif
}

DocumentSource::PasswordAsker DocumentView::askPassword()
{
#ifdef HAVE_PDF_RENDERING
    return [this](const QString &file, bool retry) -> std::optional<QString> {
        PasswordDialog prompt(QFileInfo(file).fileName(), retry, this);
        if (prompt.exec() != QDialog::Accepted) return std::nullopt;
        return prompt.password();
    };
#else
    return {};
#endif
}

void DocumentView::resetContentProvider()
{
#ifdef HAVE_PDF_RENDERING

    m_src->resetContentProvider();
    m_hover->setSource(m_src->contentProvider(), m_session);
#endif
}

bool DocumentView::pdfRenderingAvailable() const
{
#ifdef HAVE_PDF_RENDERING
    return true;
#else
    return false;
#endif
}

#ifdef HAVE_PDF_RENDERING
DocumentExporter::Sources DocumentView::exportSources() const
{
    DocumentExporter::Sources src;
    src.renderer  = m_src->renderer();
    src.provider  = m_src->contentProvider();
    src.session   = m_session;
    src.ocr       = m_ocrEngine;
    src.pageCount = m_src->pageCount();
    src.backend   = m_src->backend();
    return src;
}
#endif

QList<DocxPage> DocumentView::allPageContent(const QList<int> &pages,
                                             const DocxExportOptions &options,
                                             const std::function<bool(int, int)> &progress)
{
#ifdef HAVE_PDF_RENDERING
    return DocumentExporter(exportSources()).allPageContent(pages, options, progress);
#else
    Q_UNUSED(pages) Q_UNUSED(options) Q_UNUSED(progress)
    return {};
#endif
}

void DocumentView::setBusy(bool busy)
{
    if (busy) m_spinner->showNow();
    else      m_spinner->setBusy(m_layoutEngine->busy());
}

bool DocumentView::exportPagesToImages(const QString &outputPath, int quality,
                                       const QList<int> &pages,
                                       const std::function<bool(int, int)> &progress)
{
#ifdef HAVE_PDF_RENDERING
    return DocumentExporter(exportSources())
        .exportPagesToImages(outputPath, quality, pages, progress);
#else
    Q_UNUSED(outputPath) Q_UNUSED(quality) Q_UNUSED(pages) Q_UNUSED(progress)
    return false;
#endif
}
