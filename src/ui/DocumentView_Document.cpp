// Part of DocumentView — see DocumentView.hpp. Split across translation
// units purely for readability; one 2500-line file was not reviewable.
// The document itself: opening, saving, working copies, history, export.

#include "ui/DocumentView.hpp"

#include "app/PdfPwStore.hpp"
#include "engine/document/DocumentSource.hpp"
#include "engine/edit/InkMetrics.hpp"
#include "app/SafeWrite.hpp"
#include "app/SessionStore.hpp"
#include "ui/tools/ImageAnnotation.hpp"
#include "ui/view/ImageAnnotationLayer.hpp"
#include "ui/view/HoverHighlight.hpp"
#include "ui/view/PageLayoutEngine.hpp"
#include "ui/view/ZoomController.hpp"
#include "ui/view/TextSelectionController.hpp"
#include "ui/dialogs/PasswordDialog.hpp"

#include <QFileInfo>

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <cstring>
#endif

#ifdef HAVE_POPPLER
#  include <QPdfWriter>
#  include <QPageSize>
#  include <cmath>
#  include <algorithm>
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

void DocumentView::clearDocument()
{
    cancelCurrentEdit();
    m_selection->clear();

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
    m_src->clearContentProvider();
#endif
#ifdef HAVE_QT_PDF
    discardEditHistory();
    m_src->document()->close();
    m_ocrCache.clear();
#elif defined(HAVE_POPPLER)
    discardEditHistory();
    m_ocrCache.clear();
    setPopplerSource(nullptr);   // closes the document and frees its handles
#endif
    SessionStore::discard(m_src->contentPath());
    m_src->setContentPath(QString());
    m_journal.targetPath.clear();
    m_journal.workingCopyDirty = false;
    // The log describes a document that is no longer open; its snapshots go
    // with it, or the session directory fills up with states nothing points at.
    m_journal.history()->reset();
#ifdef HAVE_PDF_RENDERING
    // m_src->renderer() is already gone on the Poppler path — drop the stale handles.
    m_imageLayer->setSource(m_src->renderer(), m_session, m_ocrEngine, QString());
#endif
    m_src->setPageCount(0);
    m_lastReportedPage = -1;

    m_layoutEngine->clearPages();

    m_imageLayer->clear();

    m_dropHint->show();
}

bool DocumentView::openFile(const QString &path)
{
    if (path.isEmpty()) return false;
    cancelCurrentEdit();

    // Opening a document ends any working-copy state of the previous one. The
    // old working file is only dropped once the new document actually loaded.
    const QString previousWorkingFile =
        (path != m_src->contentPath()) ? m_src->contentPath() : QString();

    if (m_viewMode == ViewMode::Grid)
        setViewMode(ViewMode::Single);

#ifdef HAVE_QT_PDF
    if (!m_src->load(path, askPassword())) return false;

    discardEditHistory();
    m_journal.targetPath.clear();
    m_journal.workingCopyDirty = false;
    SessionStore::discard(previousWorkingFile);
    m_imageLayer->setSource(m_src->renderer(), m_session, m_ocrEngine, m_src->contentPath());
    m_layoutEngine->setPageCount(m_src->pageCount());
    resetContentProvider();
    m_dropHint->hide();
    m_layoutEngine->buildPages();
    // Once the scroll area has laid the new pages out, hand the engine the
    // window it actually renders — until then the page positions are all 0.
    QMetaObject::invokeMethod(this, [this]() { syncVisibleRect(); },
                              Qt::QueuedConnection);
    m_journal.noteDocumentOpened(QFileInfo(currentFile()).fileName());
    Q_EMIT fileOpened(m_src->contentPath(), m_src->pageCount());
    m_lastReportedPage = 0;          // freshly opened documents start at the top
    Q_EMIT pageChanged(1, m_src->pageCount());
    return true;

#elif defined(HAVE_POPPLER)
    auto doc = m_src->open(path, askPassword());
    if (!doc) return false;

    discardEditHistory();
    m_ocrCache.clear();
    m_src->setContentPath(path);
    m_journal.targetPath.clear();
    m_journal.workingCopyDirty = false;
    setPopplerSource(std::move(doc));
    // Only now that the reader let go of it may the working file be removed.
    SessionStore::discard(previousWorkingFile);
    m_src->setPageCount(m_src->popplerDoc()->numPages());
    m_layoutEngine->setPageCount(m_src->pageCount());
    resetContentProvider();
    m_dropHint->hide();
    m_layoutEngine->buildPages();
    // See the Qt-PDF branch: the render window is only measurable once the
    // scroll area has laid the new pages out.
    QMetaObject::invokeMethod(this, [this]() { syncVisibleRect(); },
                              Qt::QueuedConnection);
    m_journal.noteDocumentOpened(QFileInfo(currentFile()).fileName());
    Q_EMIT fileOpened(m_src->contentPath(), m_src->pageCount());
    m_lastReportedPage = 0;          // freshly opened documents start at the top
    Q_EMIT pageChanged(1, m_src->pageCount());
    return true;

#else
    m_src->setContentPath(path);
    m_journal.targetPath.clear();
    m_journal.workingCopyDirty = false;
    SessionStore::discard(previousWorkingFile);
    m_src->setPageCount(1);
    m_dropHint->show();
    retranslateUi();
    m_journal.noteDocumentOpened(QFileInfo(currentFile()).fileName());
    Q_EMIT fileOpened(m_src->contentPath(), m_src->pageCount());
    m_lastReportedPage = 0;          // freshly opened documents start at the top
    Q_EMIT pageChanged(1, m_src->pageCount());
    return true;
#endif
}

// ── Zoom / Tool / Edit mode ───────────────────────────────────────────────────

bool DocumentView::openWorkingCopy(const QString &contentPath,
                                   const QString &targetPath,
                                   const DocumentHistory::Change &change)
{
    // openFile records the state; it needs to know whether this file is a new
    // document or the result of a change to the one already open.
    m_journal.openChange = change;
    struct ClearOnReturn {
        DocumentHistory::Change *c;
        ~ClearOnReturn() { *c = DocumentHistory::Change{}; }
    } clearOnReturn { &m_journal.openChange };

    // qpdf keeps a document's encryption when it rewrites it, so the working
    // copy of a protected file is protected too — with the same password. Carry
    // it across, or the user is asked again for a file they never chose.
    if (!targetPath.isEmpty() && !PdfPwStore::has(contentPath))
        PdfPwStore::set(contentPath, PdfPwStore::get(targetPath));

    if (!openFile(contentPath)) return false;
    if (targetPath.isEmpty() || targetPath == contentPath) return true;

    m_journal.targetPath       = targetPath;
    m_journal.workingCopyDirty = true;
    // openFile announced the working file — re-announce under the document's
    // own name so the title bar shows the PDF the user opened.
    Q_EMIT fileOpened(m_journal.targetPath, m_src->pageCount());
    return true;
}

bool DocumentView::saveToFile(const QString &path)
{
#ifdef HAVE_PDF_RENDERING
    // From here on the document on disk may stop being the one the history's
    // first entry stands for, so that entry gets its own copy now.
    m_journal.history()->materializeSnapshot();
#endif
#ifdef HAVE_QT_PDF
    commitCurrentEdit(m_editorFrame->currentText());

    // Saving writes the session out; it does not end it. The document keeps
    // rendering from its own base with the edits layered on, so undo and redo
    // reach across the save for as long as the file stays open.
    const bool detached = detachSourceFrom(path);
    if (!m_session->saveToFile(path, m_src->document(), m_src->pageCount(), m_src->contentPath()))
        return false;

    if (detached) {
        m_journal.markSaved(path);
        return true;
    }

    // The base could not be put out of harm's way, so it is gone: the file we
    // read from has just been overwritten with the edits baked in. Start over
    // from the result — and drop the history with it, or undo would paint
    // those same edits on top a second time (see discardEditHistory).
    discardEditHistory();
    const QString previousWorkingFile = (path != m_src->contentPath()) ? m_src->contentPath() : QString();
    m_src->setContentPath(path);
    m_journal.targetPath.clear();
    m_journal.workingCopyDirty = false;
    m_src->document()->close();
    m_src->document()->load(path);
    // Only now that the reader let go of it may the working file be removed.
    SessionStore::discard(previousWorkingFile);
    resetContentProvider();
    m_layoutEngine->rerenderAll();
    m_journal.recordSavedOverBase(path);
    return true;
#elif defined(HAVE_POPPLER)
    commitCurrentEdit(m_editorFrame->currentText());
    if (!m_src->popplerDoc() || !m_src->renderer()) return false;

    // Saving writes the session out; it does not end it. See the Qt path above.
    const bool detached = detachSourceFrom(path);

    // The new document is written beside the target first, because the render
    // reads from the document that is still open — and that may be the very
    // file being saved to.
    const QString staging = SafeWrite::stagingPath(path);
    if (staging.isEmpty()) return false;
    if (!writePopplerRaster(staging)) { SafeWrite::discard(staging); return false; }

    // With the base moved aside, the target is not a file this process holds
    // open, so the swap needs none of the close-and-reopen dance below — the
    // one Windows forces when Poppler is still reading the file being replaced.
    if (detached) {
        if (!SafeWrite::commit(staging, path)) return false;
        m_journal.markSaved(path);
        return true;
    }

    // Windows refuses to rename or delete a file that is still open: Poppler
    // holds the document it renders from without allowing deletion, so saving
    // over the open document failed at the swap below and the save silently did
    // nothing. Let go of the file first — it is reopened either way, from the
    // new file when the swap worked and from the old one when it did not.
    const QString previousWorkingFile = (path != m_src->contentPath()) ? m_src->contentPath() : QString();
    const QString reopenPath = m_src->contentPath();
    setPopplerSource(nullptr);

    if (!SafeWrite::commit(staging, path)) {
        setPopplerSource(loadPopplerDocument(reopenPath));
        resetContentProvider();
        return false;
    }

    // Reload from the saved file so subsequent saves work on the updated
    // content. The edits are part of the file now, which is exactly why the
    // undo history goes with them — see discardEditHistory.
    discardEditHistory();
    auto reloaded = loadPopplerDocument(path);
    if (reloaded) {
        m_src->setContentPath(path);
        m_journal.targetPath.clear();
        m_journal.workingCopyDirty = false;
        setPopplerSource(std::move(reloaded));
        // Only now that the reader let go of it may the working file be removed.
        SessionStore::discard(previousWorkingFile);
    } else {
        // The file was written, but it cannot be read back. Keep showing the
        // document that was open instead of leaving the view blank — and keep
        // its working file, which is what that view is reading from.
        qWarning() << "[SAVE] wrote" << path << "but could not reopen it";
        setPopplerSource(loadPopplerDocument(reopenPath));
    }
    resetContentProvider();
    m_layoutEngine->rerenderAll();
    m_journal.recordSavedOverBase(path);
    return true;
#else
    Q_UNUSED(path)
    return false;
#endif
}

#ifdef HAVE_PDF_RENDERING
bool DocumentView::detachSourceFrom(const QString &saveTarget)
{
    if (m_src->contentPath().isEmpty() || saveTarget.isEmpty()) return false;
    // Already on a working copy — the save cannot reach it.
    if (SessionStore::isWorkingFile(m_src->contentPath())) return true;
    // Writing somewhere else entirely leaves the base alone.
    if (QFileInfo(m_src->contentPath()).absoluteFilePath()
            != QFileInfo(saveTarget).absoluteFilePath())
        return true;

    const QString work = SessionStore::newWorkingFile(m_src->contentPath());
    if (work.isEmpty()) return false;
    QFile::remove(work);
    if (!QFile::copy(m_src->contentPath(), work)) return false;
    PdfPwStore::set(work, PdfPwStore::get(m_src->contentPath()));

    const QString original = m_src->contentPath();
#ifdef HAVE_QT_PDF
    m_src->document()->close();
    m_src->document()->setPassword(PdfPwStore::get(work));
    if (m_src->document()->load(work) != QPdfDocument::Error::None) {
        m_src->document()->close();
        m_src->document()->setPassword(PdfPwStore::get(original));
        m_src->document()->load(original);
        SessionStore::discard(work);
        return false;
    }
#elif defined(HAVE_POPPLER)
    auto copy = loadPopplerDocument(work);
    if (!copy) { SessionStore::discard(work); return false; }
    setPopplerSource(std::move(copy));
#endif
    m_src->setContentPath(work);
    // The content scanner holds the old path open; on Windows that alone would
    // block the save from replacing it.
    resetContentProvider();
    return true;
}

#endif   // HAVE_PDF_RENDERING

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
    if (!m_journal.history()->canRestore(index)) return false;
    if (index == m_journal.history()->currentIndex()) return true;

    const DocumentHistory::Entry entry = m_journal.history()->entries().value(index);

    if (m_journal.history()->restoringDropsEdits(index)) {
        // The state lives in a different document file. It is reopened from a
        // copy, never from the snapshot itself — a snapshot that becomes the
        // file being edited would be modified by the next save, and the state
        // it stands for would be gone.
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
        // The reopened file carries the page state and the entry carries the
        // images; only the text edits of that moment are gone, because they
        // were never written to any file. The user agreed to that before we
        // got here (see DocumentHistory::restoringDropsEdits).
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

    // Same file: the state is a position in the session. The undo stack holds
    // the text edits, the entry itself holds the images.
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

#ifdef HAVE_POPPLER
bool DocumentView::writePopplerRaster(const QString &outputPath)
{
    if (!m_src->popplerDoc() || !m_src->renderer() || !m_session || m_src->pageCount() <= 0)
        return false;

    constexpr qreal kPts2Px = 300.0 / 72.0;  // 300 DPI

    QPdfWriter writer(outputPath);
    writer.setCreator(QStringLiteral("OpenPDF Studio"));
    writer.setResolution(300);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));

    // Page size is per page, not per document: a size taken from page 1 and
    // kept squeezes every differently sized page (a landscape sheet, a scan)
    // onto that first box. It has to be set before the page it applies to
    // begins — for the first page that means before the painter starts it.
    const auto setSizeFor = [&writer](const QSizeF &pts) {
        writer.setPageSize(QPageSize(pts, QPageSize::Point, QString(),
                                     QPageSize::ExactMatch));
    };
    setSizeFor(m_src->renderer()->pageSizePts(0));

    QPainter painter(&writer);
    if (!painter.isActive()) return false;

    for (int i = 0; i < m_src->pageCount(); ++i) {
        if (i > 0) {
            setSizeFor(m_src->renderer()->pageSizePts(i));
            if (!writer.newPage()) return false;
        }
        QImage img = m_src->renderer()->renderPage(i, kPts2Px);
        if (img.isNull()) continue;
        m_session->applyToImage(i, img, kPts2Px);
        const QRect pageRect(0, 0, painter.device()->width(), painter.device()->height());
        painter.drawImage(pageRect, img);
    }
    painter.end();
    return true;
}

std::unique_ptr<Poppler::Document> DocumentView::loadPopplerDocument(const QString &path)
{
    if (path.isEmpty()) return nullptr;
    auto doc = Poppler::Document::load(path);
    if (doc && doc->isLocked()) {
        const QByteArray known = PdfPwStore::get(path).toUtf8();
        if (!known.isEmpty()) doc->unlock(known, known);
    }
    if (!doc || doc->isLocked()) return nullptr;
    doc->setRenderHint(Poppler::Document::Antialiasing);
    doc->setRenderHint(Poppler::Document::TextAntialiasing);
    return doc;
}

void DocumentView::setPopplerSource(std::unique_ptr<Poppler::Document> doc)
{
    // Drop the handles into the renderer before the object behind it dies.
    m_selection->setSource(nullptr, nullptr);
    m_layoutEngine->setSource(nullptr, m_session);
    m_imageLayer->setSource(nullptr, m_session, m_ocrEngine, QString());

    m_src->setPopplerDoc(std::move(doc));
    if (!m_src->popplerDoc()) return;   // closed: the file is free to be replaced now

    m_selection->setSource(m_src->renderer(), m_src->popplerDoc());
    m_layoutEngine->setSource(m_src->renderer(), m_session);
    m_imageLayer->setSource(m_src->renderer(), m_session, m_ocrEngine, m_src->contentPath());
}
#endif // HAVE_POPPLER

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
    m_src->clearContentProvider();
    // The document may have been reloaded from disk.
    m_selection->invalidateCaches();
#  if defined(HAVE_QT_PDF) && defined(HAVE_QPDF)
    if (!m_src->contentPath().isEmpty())
        m_src->setContentProvider(std::make_unique<QpdfContentProvider>(
            m_src->contentPath(),
            [this](int p) { return m_src->document()->pagePointSize(p); }));
#  elif defined(HAVE_POPPLER)
    if (m_src->popplerDoc())
        m_src->setContentProvider(std::make_unique<PopplerContentProvider>(m_src->popplerDoc()));
#  endif
    m_hover->setSource(m_src->contentProvider(), m_session);
#endif
}

// ── Text selection (Select tool) ─────────────────────────────────────────────
// Lives in TextSelectionController (ui/view/). Works in normal mode and in edit
// mode: the select tool only ever reads text, it never touches the session.

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
#  ifdef HAVE_QT_PDF
    src.document  = m_src->document();
    src.extractor = m_src->extractor();
#  endif
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

