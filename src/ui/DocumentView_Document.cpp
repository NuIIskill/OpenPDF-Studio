// Part of DocumentView — see DocumentView.hpp. Split across translation
// units purely for readability; one 2500-line file was not reviewable.
// The document itself: opening, saving, working copies, history, export.

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

} // namespace

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
    // Closes the document, drops the content model and releases the file.
    m_src->close();
#endif
    SessionStore::discard(previousContent);
    m_src->setContentPath(QString());
    m_journal.targetPath.clear();
    m_journal.workingCopyDirty = false;
    m_bookmarks.clear();
    m_bookmarksDirty = false;
    Q_EMIT bookmarkDataChanged();
    // The log describes a document that is no longer open; its snapshots go
    // with it, or the session directory fills up with states nothing points at.
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

    // Opening a document ends any working-copy state of the previous one. The
    // old working file is only dropped once the new document actually loaded.
    const QString previousWorkingFile =
        (path != m_src->contentPath()) ? m_src->contentPath() : QString();

    if (m_viewMode == ViewMode::Grid)
        setViewMode(ViewMode::Single);

#ifdef HAVE_PDF_RENDERING
    if (!m_src->open(path, askPassword())) return false;

    discardEditHistory();
    // Der OCR-Zwischenspeicher ist nach Seitenzahl abgelegt, nicht nach Datei:
    // ohne dieses Leeren beantwortet Seite 3 des neuen Dokuments Anfragen mit
    // dem, was auf Seite 3 des alten stand.
    m_edit.clearOcrCache();
    m_journal.targetPath.clear();
    m_journal.workingCopyDirty = false;
    m_bookmarks      = m_src->backend()->bookmarks();
    m_bookmarksDirty = false;
    Q_EMIT bookmarkDataChanged();
    // Only now that the reader let go of it may the working file be removed.
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
    // Once the scroll area has laid the new pages out, hand the engine the
    // window it actually renders — until then the page positions are all 0.
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
    if (targetPath.isEmpty()) {
        // The content exists only in the session so far. Keep the document
        // untitled and dirty so its first regular save asks for a real path.
        m_journal.workingCopyDirty = true;
        Q_EMIT fileOpened(QString(), m_src->pageCount());
        return true;
    }
    if (targetPath == contentPath) return true;

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
    commitCurrentEdit(m_editorFrame->currentText());

    auto *backend = m_src->backend();
    if (!backend || !m_session || m_src->pageCount() <= 0) return false;

    // Saving writes the session out; it does not end it. The document keeps
    // rendering from its own base with the edits layered on, so undo and redo
    // reach across the save for as long as the file stays open.
    const bool detached = detachSourceFrom(path);

    // Written beside the target first, because the renderer reads from the
    // document that is still open — and that may be the very file being saved
    // to. Wie geschrieben wird, entscheidet das Backend; ob der Tausch sicher
    // ist, entscheidet sich hier.
    const QString staging = SafeWrite::stagingPath(path);
    if (staging.isEmpty()) return false;
    if (!backend->saveWithEdits(staging, *m_session)) {
        SafeWrite::discard(staging);
        return false;
    }

    // What the backend cannot write comes now: overlays write their part into
    // the finished staging file before it takes the document's place.
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

    // With the base moved aside, the target is not a file this process holds
    // open, so the swap needs none of the close-and-reopen dance below.
    if (detached) {
        if (!SafeWrite::commit(staging, path)) return false;
        m_bookmarksDirty = false;
        m_journal.markSaved(path);
        Q_EMIT bookmarkDataChanged();
        return true;
    }

    // Windows refuses to rename or delete a file that is still open: the
    // backend holds the document it renders from, so saving over the open
    // document failed at the swap and did silently nothing. Let go of the file
    // first — it is reopened either way, from the new file when the swap
    // worked and from the old one when it did not.
    const QString previousWorkingFile =
        (path != m_src->contentPath()) ? m_src->contentPath() : QString();
    const QString reopenPath = m_src->contentPath();
    m_src->close();

    if (!SafeWrite::commit(staging, path)) {
        m_src->open(reopenPath, nullptr);
        resetContentProvider();
        return false;
    }

    // Reload from the saved file so subsequent saves work on the updated
    // content. The edits are part of the file now, which is exactly why the
    // undo history goes with them — see discardEditHistory.
    discardEditHistory();
    if (m_src->open(path, nullptr)) {
        m_journal.targetPath.clear();
        m_journal.workingCopyDirty = false;
        // Only now that the reader let go of it may the working file be removed.
        SessionStore::discard(previousWorkingFile);
    } else {
        // The file was written, but it cannot be read back. Keep showing the
        // document that was open instead of leaving the view blank — and keep
        // its working file, which is what that view is reading from.
        qWarning() << "[SAVE] wrote" << path << "but could not reopen it";
        m_src->open(reopenPath, nullptr);
    }
    resetContentProvider();
    m_linkLayer->reload();
    m_noteLayer->reload();
    // Overlays read from the file, not from the session. After a save that is
    // a different file: what they contributed is in it now and has to be read
    // back from there, or it would stand twice.
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
    // Kein Prompt: die Kopie trägt dasselbe Passwort wie das Original, und das
    // steht seit der Zeile oben im Speicher.
    if (!m_src->open(work, nullptr)) {
        m_src->open(original, nullptr);
        SessionStore::discard(work);
        return false;
    }
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
    // Welches Modell dazu passt, weiß das Backend; hier hängt nur noch dran,
    // wer davon liest.
    m_src->resetContentProvider();
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
