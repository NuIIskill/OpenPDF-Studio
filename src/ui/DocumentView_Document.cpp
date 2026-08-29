

#include "ui/DocumentView.hpp"

#include "app/PdfPwStore.hpp"
#include "engine/document/BookmarkWriter.hpp"
#include "engine/document/DocumentSource.hpp"
#include "engine/edit/InkMetrics.hpp"
#include "app/SafeWrite.hpp"
#include "app/SessionStore.hpp"
#include "ui/tools/ImageAnnotation.hpp"
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
    const QString previousContent = m_src->contentPath();
#ifdef HAVE_PDF_RENDERING
    m_hover->hide();
    discardEditHistory();
    m_edit.clearOcrCache();

    m_src->close();
#endif
    SessionStore::discard(previousContent);
    m_src->setContentPath(QString());
    m_journal.targetPath.clear();
    m_journal.workingCopyDirty = false;
    m_bookmarks.clear();
    m_bookmarksDirty = false;
    Q_EMIT bookmarkDataChanged();

    m_journal.history()->reset();
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

bool DocumentView::openFile(const QString &path)
{
    if (path.isEmpty()) return false;
    cancelCurrentEdit();

    const QString previousWorkingFile =
        (path != m_src->contentPath()) ? m_src->contentPath() : QString();

    if (m_viewMode == ViewMode::Grid)
        setViewMode(ViewMode::Single);

#ifdef HAVE_PDF_RENDERING
    if (!m_src->open(path, askPassword())) return false;

    discardEditHistory();

    m_edit.clearOcrCache();
    m_journal.targetPath.clear();
    m_journal.workingCopyDirty = false;
    m_bookmarks      = m_src->backend()->bookmarks();
    m_bookmarksDirty = false;
    Q_EMIT bookmarkDataChanged();

    SessionStore::discard(previousWorkingFile);
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
    m_journal.noteDocumentOpened(QFileInfo(currentFile()).fileName());
    Q_EMIT fileOpened(m_src->contentPath(), m_src->pageCount());
    m_lastReportedPage = 0;
    Q_EMIT pageChanged(1, m_src->pageCount());
    return true;

#else
    m_src->setContentPath(path);
    m_journal.targetPath.clear();
    m_journal.workingCopyDirty = false;
    m_bookmarks.clear();
    m_bookmarksDirty = false;
    Q_EMIT bookmarkDataChanged();
    SessionStore::discard(previousWorkingFile);
    m_src->setPageCount(1);
    m_find->documentChanged();
    m_dropHint->show();
    retranslateUi();
    m_journal.noteDocumentOpened(QFileInfo(currentFile()).fileName());
    Q_EMIT fileOpened(m_src->contentPath(), m_src->pageCount());
    m_lastReportedPage = 0;
    Q_EMIT pageChanged(1, m_src->pageCount());
    return true;
#endif
}

bool DocumentView::openWorkingCopy(const QString &contentPath,
                                   const QString &targetPath,
                                   const DocumentHistory::Change &change)
{

    m_journal.openChange = change;
    struct ClearOnReturn {
        DocumentHistory::Change *c;
        ~ClearOnReturn() { *c = DocumentHistory::Change{}; }
    } clearOnReturn { &m_journal.openChange };

    if (!targetPath.isEmpty() && !PdfPwStore::has(contentPath))
        PdfPwStore::set(contentPath, PdfPwStore::get(targetPath));

    if (!openFile(contentPath)) return false;
    if (targetPath.isEmpty()) {

        m_journal.workingCopyDirty = true;
        Q_EMIT fileOpened(QString(), m_src->pageCount());
        return true;
    }
    if (targetPath == contentPath) return true;

    m_journal.targetPath       = targetPath;
    m_journal.workingCopyDirty = true;

    Q_EMIT fileOpened(m_journal.targetPath, m_src->pageCount());
    return true;
}

bool DocumentView::saveToFile(const QString &path)
{
#ifdef HAVE_PDF_RENDERING

    m_journal.history()->materializeSnapshot();
    commitCurrentEdit(m_editorFrame->currentText());

    auto *backend = m_src->backend();
    if (!backend || !m_session || m_src->pageCount() <= 0) return false;

    const bool detached = detachSourceFrom(path);

    const QString staging = SafeWrite::stagingPath(path);
    if (staging.isEmpty()) return false;
    if (!backend->saveWithEdits(staging, *m_session)) {
        SafeWrite::discard(staging);
        return false;
    }

    for (PageOverlay *overlay : std::as_const(m_overlays)) {
        if (overlay->writeTo(staging)) continue;
        SafeWrite::discard(staging);
        return false;
    }

    if (m_bookmarksDirty
            && !BookmarkWriter::write(staging, m_bookmarks,
                                      PdfPwStore::get(m_src->contentPath()))) {
        SafeWrite::discard(staging);
        return false;
    }

    if (detached) {
        if (!SafeWrite::commit(staging, path)) return false;
        m_bookmarksDirty = false;
        m_journal.markSaved(path);
        Q_EMIT bookmarkDataChanged();
        return true;
    }

    const QString previousWorkingFile =
        (path != m_src->contentPath()) ? m_src->contentPath() : QString();
    const QString reopenPath = m_src->contentPath();
    m_src->close();

    if (!SafeWrite::commit(staging, path)) {
        m_src->open(reopenPath, nullptr);
        resetContentProvider();
        return false;
    }

    discardEditHistory();
    if (m_src->open(path, nullptr)) {
        m_journal.targetPath.clear();
        m_journal.workingCopyDirty = false;

        SessionStore::discard(previousWorkingFile);
    } else {

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
    m_journal.recordSavedOverBase(path);
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
    Q_EMIT bookmarkDataChanged();
}

bool DocumentView::bookmarkEditingAvailable() const
{
    return m_src->pageCount() > 0 && BookmarkWriter::available()
        && editableBookmarks(m_bookmarks);
}

#ifdef HAVE_PDF_RENDERING
bool DocumentView::detachSourceFrom(const QString &saveTarget)
{
    if (m_src->contentPath().isEmpty() || saveTarget.isEmpty()) return false;

    if (SessionStore::isWorkingFile(m_src->contentPath())) return true;

    if (QFileInfo(m_src->contentPath()).absoluteFilePath()
            != QFileInfo(saveTarget).absoluteFilePath())
        return true;

    const QString work = SessionStore::newWorkingFile(m_src->contentPath());
    if (work.isEmpty()) return false;
    QFile::remove(work);
    if (!QFile::copy(m_src->contentPath(), work)) return false;
    PdfPwStore::set(work, PdfPwStore::get(m_src->contentPath()));

    const QString original = m_src->contentPath();

    if (!m_src->open(work, nullptr)) {
        m_src->open(original, nullptr);
        SessionStore::discard(work);
        return false;
    }

    resetContentProvider();
    return true;
}

#endif

QList<DocumentHistory::ImageState> DocumentView::imageStates() const
{
    QList<DocumentHistory::ImageState> out;
    const QList<ImageAnnotationLayer::Placed> placed = m_imageLayer->placedImages();
    out.reserve(placed.size());
    for (const auto &p : placed)
        out.append({ p.page, p.pdfBounds, p.image });
    return out;
}

bool DocumentView::restoreHistoryState(int index)
{

    closeEditorBeforeUndo();
    if (!m_journal.history()->canRestore(index)) return false;
    if (index == m_journal.history()->currentIndex()) return true;

    const DocumentHistory::Entry entry = m_journal.history()->entries().value(index);

    if (m_journal.history()->restoringDropsEdits(index)) {

        const QString snapshot = m_journal.history()->baseFileFor(index);
        const QString work     = SessionStore::newWorkingFile(currentFile());
        if (snapshot.isEmpty() || work.isEmpty()) return false;
        QFile::remove(work);
        if (!QFile::copy(snapshot, work)) return false;
#ifdef HAVE_PDF_RENDERING
        PdfPwStore::set(work, PdfPwStore::get(currentFile()));
#endif

        const QString target = currentFile();
        m_journal.restoring = true;
        const bool ok = openWorkingCopy(work, target);
        m_journal.restoring = false;
        if (!ok) {
            SessionStore::discard(work);
            return false;
        }

#ifdef HAVE_PDF_RENDERING
        QList<ImageAnnotationLayer::Placed> restored;
        restored.reserve(entry.images.size());
        for (const DocumentHistory::ImageState &s : entry.images)
            restored.append({ s.page, s.pdfBounds, s.image });
        m_imageLayer->restoreImages(restored);
#endif
        m_journal.history()->setCurrentIndex(index);
        m_journal.workingCopyDirty = true;
        return true;
    }

    m_journal.restoring = true;
    m_undoStack->setIndex(entry.undoIndex);
#ifdef HAVE_PDF_RENDERING
    QList<ImageAnnotationLayer::Placed> images;
    images.reserve(entry.images.size());
    for (const DocumentHistory::ImageState &s : entry.images)
        images.append({ s.page, s.pdfBounds, s.image });
    m_imageLayer->restoreImages(images);
#endif
    m_journal.restoring = false;

    m_journal.history()->setCurrentIndex(index);
    return true;
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
