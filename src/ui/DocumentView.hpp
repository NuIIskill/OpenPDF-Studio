#pragma once

#include <QHash>
#include <QPixmap>
#include <QScrollArea>
#include <QUndoStack>

#include <memory>

QT_BEGIN_NAMESPACE
class QLabel;
class QVBoxLayout;
class QRubberBand;
class QFrame;
#ifdef HAVE_QT_PRINT
class QPrinter;
#endif
QT_END_NAMESPACE

class ImageAnnotationLayer;
class LinkAnnotationLayer;
class NoteLayer;
class DrawingLayer;
class PageOverlay;
class PageLayoutEngine;
class HoverHighlight;
class FindController;
class TextSelectionController;
class ZoomController;

#include "engine/document/DocumentJournal.hpp"
#include "engine/document/PdfBookmark.hpp"
#include "engine/document/DocumentSource.hpp"
#include "ui/edit/EditController.hpp"
#include "app/DocumentHistory.hpp"
#include "engine/ocr/OcrEngine.hpp"
#include "engine/edit/DocxExporter.hpp"
#include "engine/edit/DocumentExporter.hpp"
#include "ui/view/PageCanvas.hpp"
#include "ui/notes/Note.hpp"
#include "ui/draw/DrawTool.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/render/PdfRenderer.hpp"
#  include "engine/edit/EditSession.hpp"
#  include "ui/edit/TextBoxFrame.hpp"
#  include "engine/edit/TextBlock.hpp"
#  include "engine/edit/ContentModel.hpp"
#  include <memory>
#endif

class DocumentView : public QScrollArea, public PageCanvas
{
    Q_OBJECT

public:
    enum class Tool     { Select, Pan, Text, Comment, Draw, Image, Attach };
    enum class ViewMode { Single, Grid };

    explicit DocumentView(QWidget *parent = nullptr);
    ~DocumentView() override;

    bool   openFile(const QString &path);
    /// Shows `contentPath` while the document keeps `targetPath` as its
    /// identity and save target. Used for changes that only exist as a written
    /// PDF (page reordering, rotation, insertion): the reorganized file is a
    /// session working copy, the user's PDF stays untouched until they save.
    /// The document counts as having unsaved changes until then.
    ///
    /// `change` describes what produced the new file. Passing one keeps the
    /// change history running and files the working copy under it; without one
    /// the document counts as newly opened and the history starts over.
    bool   openWorkingCopy(const QString &contentPath, const QString &targetPath,
                           const DocumentHistory::Change &change = {});
    void   clearDocument();
    void   setZoom(int percent);
    void   setZoomSettings(int step, bool ctrlWheel, bool toPointer,
                           const QString &wheelAction);
    void   setTool(Tool tool);
    /// The chosen tool's id as it stands in the sidebar catalogue. Only for
    /// overlays, whose tools the enum above does not know.
    void   setActiveToolId(const QString &toolId);
    void   setEditMode(bool on);
    void   setViewMode(ViewMode mode);
    bool   saveToFile(const QString &path);
    void   retranslateUi();
    void   refreshTheme();
    // Called by MainWindow when the user changes the font size in the FormatBar.
    void   setEditorFontSize(int ptSize);
    // Called by MainWindow when the user changes the text color in the FormatBar.
    void   setEditorTextColor(const QColor &color);
    // Called by MainWindow on FormatBar font family / bold / italic changes.
    void   setEditorFontFamily(const QString &family);
    void   setEditorBold(bool on);
    void   setEditorItalic(bool on);
    void   setTextBoxProperties(const TextBoxProperties &properties);
    void   setTextBoxDefaults(const TextBoxProperties &properties);
    void   setEditorAlignment(Qt::Alignment alignment);
    void   setEditorListStyle(TextBoxProperties::ListStyle style);
    void   changeEditorIndent(int delta);
    void   setEditorLineSpacing(double multiplier);
    void   setDrawTool(DrawTool tool);
    void   setDrawColor(const QColor &color);
    void   setDrawWidth(qreal widthPt);

    /// Document identity and save target — the file the user opened, which is
    /// not necessarily the file currently being rendered (see contentFile()).
    QString     currentFile()      const
    {
        if (m_journal.workingCopyDirty && m_journal.targetPath.isEmpty()) return {};
        return m_journal.targetPath.isEmpty() ? m_src->contentPath() : m_journal.targetPath;
    }
    /// The PDF on disk the view is reading from. This is what anything that
    /// needs the current page content (export, presentation, organizer) must
    /// open — it holds the working copy while changes are uncommitted.
    QString     contentFile()      const { return m_src->contentPath(); }
    int         pageCount()        const override { return m_src->pageCount(); }
    // 0-based index of the page the user is looking at.
    int         currentPage()      const;
    // Scrolls the given 0-based page to the top of the viewport.
    void        goToPage(int page);
    const QList<PdfBookmark> &bookmarks() const { return m_bookmarks; }
    void        setBookmarks(const QList<PdfBookmark> &bookmarks);
    bool        bookmarkEditingAvailable() const;
    ViewMode    viewMode()         const { return m_viewMode; }
    QUndoStack *undoStack()        const { return m_undoStack; }
    QRectF editBounds() const;
    QRectF editFrameRect() const;
    double editFontSizePt() const;
    /// Change log of the open document — what the history panel shows.
    DocumentHistory *history()     const { return m_journal.history(); }
    /// Puts the document back into the state history entry `index` describes.
    /// Text and image edits are taken from the session, page changes by
    /// reopening the snapshot that entry belongs to. False when the state
    /// could not be restored; the document is then left as it was.
    bool        restoreHistoryState(int index);
    bool        hasUnsavedEdits()  const
    { return m_journal.hasUnsavedEdits() || m_bookmarksDirty; }
    bool        pdfRenderingAvailable() const;
    QList<DocxPage> allPageContent(const QList<int> &pages = {});
    bool exportPagesToImages(const QString &outputPath, int quality = 85,
                             const QList<int> &pages = {});
#ifdef HAVE_QT_PRINT
    /// Prints the document as the user currently sees it, uncommitted edits
    /// included. `pages` holds zero-based indices; empty means the whole file.
    bool printDocument(QPrinter *printer, const QList<int> &pages = {});
#endif
    // Called by undo/redo commands to refresh a page after session state is restored.
    void        rerenderPage(int page);
    // Select tool: text marked on the page (empty when nothing is selected).
    QString     selectedText() const;
    void        copySelectedText();
    void        openFind();
    QList<NoteData> notes() const;
    void createNote();
    void selectNote(const QString &id);
    void updateNote(const QString &id, const QString &title, const QString &text);
    void deleteNote(const QString &id);
    void setNotePinned(const QString &id, bool pinned);

    // ── PageCanvas ────────────────────────────────────────────────────────────
    QWidget *canvasWidget()   const override { return m_canvas; }
    QLabel  *pageLabel(int page) const override;
    int      pageLabelCount() const override;
    qreal    screenScale()    const override;
    std::pair<int, QLabel *> pageAtCanvasPos(const QPoint &canvasPos) const override;

Q_SIGNALS:
    void fileOpened(const QString &path, int pageCount);
    /// A PDF was dropped onto the view. The view does not open it
    /// itself — that is a document-management decision.
    void pdfDropped(const QString &path);
    void pageChanged(int current, int total);
    void viewModeChanged(ViewMode mode);
    void zoomChanged(int percent);
    // Emitted when an editor opens so the FormatBar can show the correct font size.
    void editorFontSizeChanged(int ptSize);
    // Emitted when an editor opens with the detected font of the clicked block.
    void editorFontChanged(const QString &family, bool bold, bool italic);
    void textBoxPropertiesChanged(const TextBoxProperties &properties);
    void textBoxEditingChanged(bool active);
    void bookmarkDataChanged();
    void notesChanged(const QList<NoteData> &notes);
    void noteSelected(const QString &id);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void changeEvent(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    // Re-render a page with the active edit's original text blanked out.
    void   rerenderPageWithBlank(int page, const QRectF &pdfBoundsPts);

    // The visible part of the canvas, in canvas coordinates. Drives which
    // pages the layout engine keeps rendered.
    QRect  visibleCanvasRect() const;
    void   syncVisibleRect();
    // Puts everything anchored to a page back where it belongs after a zoom.
    void   repositionForZoom();
    void   repositionEditorFrame();
    // Every overlay that is anchored to a page position, back onto its page.
    // Called whenever the pages move under them: a relayout, a zoom, and a
    // resize of the view — the last one because the canvas is CENTRED in the
    // viewport, so opening a side panel shifts every page sideways without
    // any of the layout signals firing.
    void   repositionPageOverlays();
    void   scrollToSearchMatch(int page, const QRectF &bounds);

#ifdef HAVE_PDF_RENDERING
    // Bundles the engine-level objects the exporter borrows.
    DocumentExporter::Sources exportSources() const;
#endif

    // Context menu for the open editor / marked page text. Images bring their
    // own menu — that one lives in ImageAnnotationLayer.
    void   showGeneralContextMenu(const QPoint &globalPos);
    // 0-based index of the first page whose top is at or below the scroll
    // position — what the image scan treats as "the visible page".
    int    firstVisiblePage() const;

    // Edit-mode interaction
    void   handleEditClick(const QPoint &canvasPos);
    void   createTextFrame(const QRect &viewportDragRect);
    void   commitCurrentEdit(const QString &newText);
    void   cancelCurrentEdit();

#ifdef HAVE_PDF_RENDERING
    // Opening an edit runs through a fixed sequence of decisions — what was
    // clicked, how big the text is, where its pen sits, what colors surround
    // it — and each step reads what the earlier ones worked out. EditOpen
    // carries that between them; it lives only for one handleEditClick().
    struct EditOpen;
    // Resolves a click into the text to edit, from the session, the region
    // model, the extractor, OCR or the content stream — in that order.
    // False means there is nothing editable under the cursor.
    bool   resolveEditTarget(const QPoint &canvasPos, EditOpen &o);
    // Bounds, form-field mode and whole-paragraph expansion from the region model.
    void   applyEditTargetBounds(EditOpen &o);
    // Font size, family and style, calibrated against the original ink.
    void   chooseEditorFont(EditOpen &o);
    // Pen/baseline position the replacement text is written from.
    void   anchorEditOrigin(EditOpen &o);
    // Grows the box to the full rendered line height.
    void   fitEditHeight(EditOpen &o);
    // Text color for the editor, background color for the blank fill.
    void   sampleEditColors(EditOpen &o);
    // Widens the box so the editor font renders every original line unwrapped.
    void   fitEditWidth(EditOpen &o);
    // Blanks the original text and opens the editor frame over it.
    void   presentEditor(EditOpen &o);
#endif

    /// The prompt DocumentSource uses while an encrypted file refuses to
    /// open. Lives here because it is a dialog: the source knows when to ask
    /// and what to do with the answer, the view knows how to ask.
    DocumentSource::PasswordAsker askPassword();

    // (Re)build the content-detection provider for the current file.
    void   resetContentProvider();
    // Apply the current font state to the open editor widget.
    void   refreshEditorFontLive();

    // Canvas helpers
    // Emits pageChanged() when scrolling brought a different page to the front.
    void reportCurrentPage();
    void scrollToPage(int page, bool allowRetry);

    // Session image overlays in the form the history stores them.
    QList<DocumentHistory::ImageState> imageStates() const;


    // Widgets
    QWidget     *m_canvas    { nullptr };
    QVBoxLayout *m_layout    { nullptr };
    QLabel      *m_dropHint  { nullptr };
    QRubberBand *m_rubberBand{ nullptr };

    // Owns the page widgets — single column and grid.
    PageLayoutEngine *m_layoutEngine { nullptr };

    // Placed images and detected image regions, tracked in PDF coordinate space.
    ImageAnnotationLayer *m_imageLayer { nullptr };
    LinkAnnotationLayer  *m_linkLayer  { nullptr };
    NoteLayer            *m_noteLayer  { nullptr };
    DrawingLayer         *m_drawingLayer { nullptr };
    // Hover feedback for detected regions in edit mode.
    HoverHighlight *m_hover { nullptr };

    // Layers contributed by optional parts of the program. Empty when none
    // were built.
    QList<PageOverlay *> m_overlays;

    // Grid view — the widgets live in m_layoutEngine; the mode itself stays
    // here because it drives which widget the scroll area shows.
    ViewMode  m_viewMode   { ViewMode::Single };
    QWidget  *m_gridCanvas { nullptr };

    // The open document: file on disk, backend object, renderer, extractor and
    // content model. Owns their lifetime and their teardown order.
    std::unique_ptr<DocumentSource> m_src;

    // Save target, dirty flag and change log of the open document.
    DocumentJournal m_journal;

    QList<PdfBookmark> m_bookmarks;
    bool               m_bookmarksDirty { false };

    int     m_lastReportedPage { -1 };   // 0-based; -1 = nothing reported yet
    Tool    m_tool      { Tool::Select };
    bool    m_editMode  { false };

    // Zoom level, wheel policy and scroll anchoring.
    ZoomController *m_zoomCtl { nullptr };

    // View-mode interaction
    QPoint m_panStart;
    QPoint m_panScrollOrigin;

    // Select-tool text marking, including its highlight overlays.
    TextSelectionController *m_selection { nullptr };

    // Floating Ctrl+F bar and its page-anchored result highlights.
    FindController *m_find { nullptr };

    // Text-tool drag-to-create state (viewport coords)
    bool   m_textTracking { false };
    bool   m_textDragging { false };
    QPoint m_textDragStart;

    // The open edit: which block, how it looks, where its pen sits.
    EditController m_edit;

    QUndoStack *m_undoStack      { nullptr };


    OcrEngine *m_ocrEngine { nullptr };

#ifdef HAVE_PDF_RENDERING
    // Builds a session edit from the active editor state (font, colors,
    // form-field binding). Single source of truth for commit AND live paths.

    // Drops the pending edits AND the undo history together. Every command on
    // the stack is written against one particular state of the document, so
    // whenever that state is replaced — a file is opened, or a save bakes the
    // edits into the PDF and reloads it — the history describes a document
    // that no longer exists. Undoing then paints those edits on top of a page
    // that already contains them, and the text appears twice. The two must
    // never be cleared apart.
    void discardEditHistory();

    // Makes sure writing to `saveTarget` cannot destroy the document the
    // session is layered on. Saving over the file that is open would pull the
    // ground out from under every pending edit and every undo command, which
    // is why the base is moved into a session working copy first — byte for
    // byte the same document, so nothing on screen changes. Returns false only
    // when the copy could not be made; the caller then falls back to writing
    // the edits away and starting a fresh session.
    bool detachSourceFrom(const QString &saveTarget);



    EditSession  *m_session     { nullptr };
    TextBoxFrame *m_editorFrame { nullptr };
#endif
};
