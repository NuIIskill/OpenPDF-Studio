

#include "ui/DocumentView.hpp"

#include "app/PdfPwStore.hpp"
#include "engine/document/BookmarkWriter.hpp"
#include "engine/document/DocumentSource.hpp"
#include "engine/edit/InkMetrics.hpp"
#include "app/SafeWrite.hpp"
#include "app/SessionStore.hpp"
#include "ui/view/ImageAnnotation.hpp"
#include "ui/view/ImageAnnotationLayer.hpp"
#include "ui/view/LinkAnnotationLayer.hpp"
#include "ui/notes/NoteLayer.hpp"
#include "ui/draw/DrawingLayer.hpp"
#include "ui/view/FindController.hpp"
#include "ui/view/PageOverlay.hpp"
#include "ui/view/HoverHighlight.hpp"
#include "ui/view/PageLayoutEngine.hpp"
#include "ui/view/ZoomController.hpp"
#include "ui/view/TextSelectionController.hpp"
#include "ui/widgets/PasswordDialog.hpp"

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
    m_journal.closed();
    m_bookmarks.clear();
    m_bookmarksDirty = false;
    Q_EMIT bookmarkDataChanged();

#ifdef HAVE_PDF_RENDERING
    m_imageLayer->setSource(m_src->renderer(), m_session, m_ocrEngine, QString());
#endif
    m_src->setPageCount(0);
    m_lastReportedPage = -1;

    m_layoutEngine->clearPages();

    m_imageLayer->clear();
    m_linkLayer->clear();
    m_noteLayer->clear();
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

bool DocumentView::openContent(const QString &path, const QString &suggestedPath,
                               const DocumentHistory::Change &change)
{
    if (path.isEmpty()) return false;
    cancelCurrentEdit();

    if (m_viewMode == ViewMode::Grid)
        setViewMode(ViewMode::Single);

#ifdef HAVE_PDF_RENDERING
    if (!m_src->open(path, askPassword())) return false;

    discardEditHistory();

    m_edit.clearOcrCache();
    m_journal.opened(suggestedPath);
    m_bookmarks      = m_src->backend()->bookmarks();
    m_bookmarksDirty = false;
    Q_EMIT bookmarkDataChanged();

    m_imageLayer->setSource(m_src->renderer(), m_session, m_ocrEngine, m_src->contentPath());
    for (PageOverlay *overlay : std::as_const(m_overlays))
        overlay->setDocument(m_src->contentPath());
    m_layoutEngine->setPageCount(m_src->pageCount());
    resetContentProvider();
    m_dropHint->hide();
    m_layoutEngine->buildPages();
    m_find->documentChanged();
    m_linkLayer->reload();
    m_noteLayer->reload();

    QMetaObject::invokeMethod(this, [this]() { syncVisibleRect(); },
                              Qt::QueuedConnection);
    m_journal.noteDocumentOpened(change);
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

bool DocumentView::saveToFile(const QString &path)
{
#ifdef HAVE_PDF_RENDERING

    m_journal.prepareSave();
    commitCurrentEdit(m_editorFrame->currentText());

    if (!m_src->backend() || !m_session || m_src->pageCount() <= 0) return false;

    const bool detached = detachSourceFrom(path);

    const QString staging = stageDocument(path);
    if (staging.isEmpty()) return false;

    if (detached) {
        if (!SafeWrite::commit(staging, path)) return false;
        m_bookmarksDirty = false;
        m_journal.markSaved(path);
        Q_EMIT bookmarkDataChanged();
        return true;
    }

    const QString reopenPath = m_src->contentPath();
    m_src->close();

    if (!SafeWrite::commit(staging, path)) {
        m_src->open(reopenPath, nullptr);
        resetContentProvider();
        return false;
    }

    discardEditHistory();
    const bool reopened = m_src->open(path, nullptr);
    if (!reopened) {
        qWarning() << "[SAVE] wrote" << path << "but could not reopen it";
        m_src->open(reopenPath, nullptr);
    }
    resetContentProvider();
    m_linkLayer->reload();
    m_noteLayer->reload();

    for (PageOverlay *overlay : std::as_const(m_overlays))
        overlay->setDocument(m_src->contentPath());
    if (m_src->backend()) m_bookmarks = m_src->backend()->bookmarks();
    m_bookmarksDirty = false;
    Q_EMIT bookmarkDataChanged();
    m_layoutEngine->rerenderAll();
    m_journal.savedOverBase(path, reopened);
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

    if (!backend->saveWithEdits(staging, *m_session)) {
        SafeWrite::discard(staging);
        return {};
    }

    for (PageOverlay *overlay : std::as_const(m_overlays)) {
        if (overlay->writeTo(staging)) continue;
        SafeWrite::discard(staging);
        return {};
    }

    if (m_bookmarksDirty
            && !BookmarkWriter::write(staging, m_bookmarks,
                                      PdfPwStore::get(m_src->contentPath()))) {
        SafeWrite::discard(staging);
        return {};
    }
    return staging;
}
#endif

bool DocumentView::writeRecoveryCopy(const QString &path)
{
#ifdef HAVE_PDF_RENDERING
    if (path.isEmpty() || path == m_src->contentPath()) return false;

    const QString staging = stageDocument(path);
    if (staging.isEmpty()) return false;
    return SafeWrite::commit(staging, path);
#else
    Q_UNUSED(path)
    return false;
#endif
}

#ifdef HAVE_PDF_RENDERING
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

QList<DocxPage> DocumentView::allPageContent(const QList<int> &pages)
{
#ifdef HAVE_PDF_RENDERING
    return DocumentExporter(exportSources()).allPageContent(pages);
#else
    Q_UNUSED(pages)
    return {};
#endif
}

bool DocumentView::exportPagesToImages(const QString &outputPath, int quality,
                                       const QList<int> &pages)
{
#ifdef HAVE_PDF_RENDERING
    return DocumentExporter(exportSources())
        .exportPagesToImages(outputPath, quality, pages);
#else
    Q_UNUSED(outputPath) Q_UNUSED(quality) Q_UNUSED(pages)
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
