

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
#include <QStringList>
#include <QKeyEvent>
#include <algorithm>
#include <limits>

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

    m_dropHint->show();
}

QString DocumentView::displayName() const
{
    return m_journal.displayName();
}

bool DocumentView::openFile(const QString &path, const QString &suggestedPath)
{
    return openContent(path, suggestedPath, {});
}

void DocumentView::setProtection(const PdfProtection &protection)
{
    m_protectionLayer->set(protection);
}

const std::optional<PdfProtection> &DocumentView::pendingProtection() const
{
    return m_protectionLayer->pending();
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
    m_dropHint->hide();
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
    m_dropHint->show();
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

    if (change.kind != DocumentHistory::Kind::Opened && pageCount() > 0)
        m_journal.prepareAnchor([this](const QString &path) { return writeRecoveryCopy(path); });
    if (!openContent(contentPath, suggestedPath, change)) {
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

DocumentHistory::DocumentState DocumentView::documentState() const
{
    DocumentHistory::DocumentState state;
    const QList<ImageAnnotationLayer::Placed> placed = m_imageLayer->placedImages();
    state.images.reserve(placed.size());
    for (const auto &p : placed)
        state.images.append({ p.page, p.pdfBounds, p.image });
    state.bookmarks = m_bookmarks;
    if (const QByteArray changes = m_imageLayer->changeState(); !changes.isEmpty())
        state.overlays.insert(ImageAnnotationLayer::changesKey(), changes);
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
    m_imageLayer->restoreChanges(state.overlays.value(ImageAnnotationLayer::changesKey()));
#endif
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

#ifdef HAVE_QT_PRINT
bool DocumentView::printDocument(QPrinter *printer, const QList<int> &pages)
{
#  ifdef HAVE_PDF_RENDERING
    return DocumentExporter(exportSources()).printPages(printer, pages);
#  else
    Q_UNUSED(printer) Q_UNUSED(pages)
    return false;
#  endif
}
#endif
