#pragma once

#include <QHash>
#include <QPixmap>
#include <QScrollArea>
#include <QUndoStack>

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
class PageLayoutEngine;
class TextSelectionController;

#include "app/DocumentHistory.hpp"
#include "engine/ocr/OcrEngine.hpp"
#include "engine/edit/DocxExporter.hpp"
#include "engine/edit/DocumentExporter.hpp"
#include "ui/view/PageCanvas.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/view/PdfRenderer.hpp"
#  include "engine/edit/PdfTextExtractor.hpp"
#  include "engine/edit/EditSession.hpp"
#  include "engine/edit/TextBoxFrame.hpp"
#  include "engine/edit/TextBlock.hpp"
#  include "engine/edit/ContentModel.hpp"
#  include <memory>
#  ifdef HAVE_QT_PDF
#    include <QPdfDocument>
#  elif defined(HAVE_POPPLER)
#    include <poppler/qt6/poppler-qt6.h>
#  endif
#endif

class DocumentView : public QScrollArea, public PageCanvas
{
    Q_OBJECT

public:
    enum class Tool     { Select, Pan, Text, Comment, Image };
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
    void   setEditMode(bool on);
    void   setViewMode(ViewMode mode);
    bool   saveToFile(const QString &path);
    void   retranslateUi();
    // Called by MainWindow when the user changes the font size in the FormatBar.
    void   setEditorFontSize(int ptSize);
    // Called by MainWindow when the user changes the text color in the FormatBar.
    void   setEditorTextColor(const QColor &color);
    // Called by MainWindow on FormatBar font family / bold / italic changes.
    void   setEditorFontFamily(const QString &family);
    void   setEditorBold(bool on);
    void   setEditorItalic(bool on);

    /// Document identity and save target — the file the user opened, which is
    /// not necessarily the file currently being rendered (see contentFile()).
    QString     currentFile()      const
    { return m_targetPath.isEmpty() ? m_filePath : m_targetPath; }
    /// The PDF on disk the view is reading from. This is what anything that
    /// needs the current page content (export, presentation, organizer) must
    /// open — it holds the working copy while changes are uncommitted.
    QString     contentFile()      const { return m_filePath; }
    int         pageCount()        const override { return m_pageCount; }
    // 0-based index of the page the user is looking at.
    int         currentPage()      const;
    // Scrolls the given 0-based page to the top of the viewport.
    void        goToPage(int page);
    ViewMode    viewMode()         const { return m_viewMode; }
    QUndoStack *undoStack()        const { return m_undoStack; }
    /// Change log of the open document — what the history panel shows.
    DocumentHistory *history()     const { return m_history; }
    /// Puts the document back into the state history entry `index` describes.
    /// Text and image edits are taken from the session, page changes by
    /// reopening the snapshot that entry belongs to. False when the state
    /// could not be restored; the document is then left as it was.
    bool        restoreHistoryState(int index);
    bool        hasUnsavedEdits()  const;
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

    // ── PageCanvas ────────────────────────────────────────────────────────────
    QWidget *canvasWidget()   const override { return m_canvas; }
    QLabel  *pageLabel(int page) const override;
    int      pageLabelCount() const override;
    qreal    screenScale()    const override;
    std::pair<int, QLabel *> pageAtCanvasPos(const QPoint &canvasPos) const override;

Q_SIGNALS:
    void fileOpened(const QString &path, int pageCount);
    void pageChanged(int current, int total);
    void viewModeChanged(ViewMode mode);
    void zoomChanged(int percent);
    // Emitted when an editor opens so the FormatBar can show the correct font size.
    void editorFontSizeChanged(int ptSize);
    // Emitted when an editor opens with the detected font of the clicked block.
    void editorFontChanged(const QString &family, bool bold, bool italic);

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

    // Zoom, keeping the document point under `viewportAnchor` in place.
    void   applyZoom(int percent, const QPoint &viewportAnchor);
    // The visible part of the canvas, in canvas coordinates. Drives which
    // pages the layout engine keeps rendered.
    QRect  visibleCanvasRect() const;
    void   syncVisibleRect();
    // Make the scroll area take the new page sizes into account NOW — it would
    // otherwise resize its canvas (and with it the scrollbar ranges) only once
    // the event loop runs, and any setValue() until then is clamped to the old
    // range.
    void   updateScrollRange();

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
    // Hover feedback: outline the detected content region under the cursor.
    void   updateHoverHighlight(const QPoint &canvasPos);
    void   hideHoverHighlight();
    // (Re)build the content-detection provider for the current file.
    void   resetContentProvider();
    // Apply the current font state to the open editor widget.
    void   refreshEditorFontLive();

    // Canvas helpers
    // Clamp r so it stays fully inside the PDF page (both position and size).
    void clampToPdfPage(int page, QRectF &r) const;
    // Emits pageChanged() when scrolling brought a different page to the front.
    void reportCurrentPage();
    void scrollToPage(int page, bool allowRetry);

    // ── Change history ────────────────────────────────────────────────────────
    // Records a state the user can come back to. Every place that changes the
    // document calls this, which is why it is one line: a change that forgets
    // to report itself is a gap nothing can fill in later.
    void recordChange(const DocumentHistory::Change &c,
                      const QString &snapshotSource = QString());
    // Called by openFile once the document is on screen. Either starts a fresh
    // history or files the new file under m_openChange (see openWorkingCopy).
    void noteDocumentOpened();
    // Records a save that had to overwrite the document it was reading from,
    // so the written file becomes the state the history points at.
    void recordSavedOverBase(const QString &path);
    // Session image overlays in the form the history stores them.
    QList<DocumentHistory::ImageState> imageStates() const;

#ifdef HAVE_POPPLER
    // Renders every page into a fresh PDF at `outputPath`, edits applied.
    // Writes nothing but that file — the caller owns the swap onto the target,
    // which must happen with the document closed (see saveToFile).
    bool writePopplerRaster(const QString &outputPath);
    // Loads `path` with a password already known for it. No prompting: this is
    // for reopening a file the user has already unlocked.
    std::unique_ptr<Poppler::Document> loadPopplerDocument(const QString &path);
    // THE place where the open document is exchanged. Everything that keeps a
    // raw pointer to the document or the renderer is re-pointed here, in the
    // same breath as the swap — a holder left behind reads freed memory on the
    // next repaint. Passing nullptr closes the document and releases the file.
    void setPopplerSource(std::unique_ptr<Poppler::Document> doc);
#endif

    // Widgets
    QWidget     *m_canvas    { nullptr };
    QVBoxLayout *m_layout    { nullptr };
    QLabel      *m_dropHint  { nullptr };
    QRubberBand *m_rubberBand{ nullptr };

    // Owns the page widgets — single column and grid.
    PageLayoutEngine *m_layoutEngine { nullptr };

    // Placed images and detected image regions, tracked in PDF coordinate space.
    ImageAnnotationLayer *m_imageLayer { nullptr };

    // Grid view — the widgets live in m_layoutEngine; the mode itself stays
    // here because it drives which widget the scroll area shows.
    ViewMode  m_viewMode   { ViewMode::Single };
    QWidget  *m_gridCanvas { nullptr };

    // Document state
    QString m_filePath;      // file being rendered (may be a session working copy)
    QString m_targetPath;    // save target while m_filePath is a working copy
    // Changes that live only in the working copy, not in m_session — they make
    // the document dirty even though the session holds no edits.
    bool    m_workingCopyDirty { false };
    int     m_zoom      { 100 };
    int     m_pageCount { 0 };
    int     m_lastReportedPage { -1 };   // 0-based; -1 = nothing reported yet
    Tool    m_tool      { Tool::Select };
    bool    m_editMode  { false };

    // Zoom settings (from AppSettings via MainWindow)
    int     m_zoomStep          { 10 };
    bool    m_ctrlWheelEnabled  { true };
    bool    m_zoomToPointer     { true };
    QString m_wheelAction       { QStringLiteral("scroll") };

    // View-mode interaction
    QPoint m_panStart;
    QPoint m_panScrollOrigin;

    // Select-tool text marking, including its highlight overlays.
    TextSelectionController *m_selection { nullptr };

    // Text-tool drag-to-create state (viewport coords)
    bool   m_textTracking { false };
    bool   m_textDragging { false };
    QPoint m_textDragStart;

    // Edit-mode state
    int     m_activeEditPage { -1 };        // page under the box NOW (follows drags)
    // Page the edit was OPENED on. The box may be dragged onto another page:
    // the blank (erasing the original text) always belongs to the source page,
    // the new text is committed on m_activeEditPage.
    int     m_activeEditSourcePage { -1 };
    QRectF  m_activeEditBounds;
    QRectF  m_activeEditOriginalBounds;  // native PDF bounds when the edit was first opened
    // Tight glyph rects of the original text — erasure targets ONLY these,
    // never the whole bounds rect (which would wipe co-located graphics).
    QList<QRectF> m_activeEditEraseRects;
    // true  → edit was opened by clicking on existing text (handleEditClick); a blank
    //         edit must be committed to erase the original text before drawing the new.
    // false → editor was created fresh via drag (createTextFrame); no erasure needed —
    //         the text is drawn as a transparent overlay so it can sit on top of content.
    bool    m_activeEditNeedsBlank { false };
    // true → the editor was opened on content that is already in the document
    // (handleEditClick). Committing such an edit erases the original, so a
    // commit that changes NOTHING must be dropped instead: re-drawing identical
    // text would still swap the embedded font for ours (see commitCurrentEdit).
    bool    m_activeEditInPlace { false };
    QString m_activeEditOriginalText;
    // The text as the DOCUMENT had it, which survives every re-edit — unlike
    // m_activeEditOriginalText, which becomes whatever was typed last. It is
    // the proof of which glyphs the file's own font carries, so re-editing a
    // line must not let characters we typed ourselves pass as evidence.
    QString m_activeEditPdfText;
    // State as the editor was PRESENTED — the reference for "did anything
    // actually change?". Bounds are the presented ones (they can be wider than
    // m_activeEditOriginalBounds, which stays at the original glyph rect).
    QRectF  m_activeEditPresentedBounds;
    double  m_activeEditPresentedFontSizePt { 0.0 };
    QColor  m_activeEditPresentedColor;
    QString m_activeEditFieldName;   // non-empty: editing an AcroForm text field
    // Where the text being edited starts (x = pen, y = baseline), as an offset
    // from m_activeEditBounds' top-left so moving or resizing the frame carries
    // it along. This is what both paint paths align to; without it the
    // replacement lands a few points below and right of the line it replaces.
    QPointF m_activeEditOriginOffset;
    bool    m_activeEditHasOrigin { false };
    // Baseline-to-baseline distance of the block being edited (0 = unknown).
    double  m_activeEditLineSpacingPt { 0.0 };
    // Two sizes, on purpose (see EditSession::Edit): the one the file gets and
    // the one the screen gets. They differ whenever the family being painted
    // is not the embedded one, which is most of the time.
    double  m_currentEditorFontSizePt   { 12.0 };
    double  m_currentEditorRenderSizePt { 12.0 };
    bool    m_editorSizeChangedByUser   { false };
    QColor  m_currentEditorColor     { 0x11, 0x11, 0x11 };
    QColor  m_currentBgColor;  // cell background detected from PDF content stream
    // Font of the active editor (detected from the clicked block, or chosen
    // in the FormatBar). m_editorFontChangedByUser gates the vector-save font
    // switch: only a user choice replaces the PDF's original font resource.
    QString m_currentEditorFontFamily;
    bool    m_currentEditorBold   { false };
    bool    m_currentEditorItalic { false };
    bool    m_editorFontChangedByUser { false };

    // After a commit, the press+release that triggered it must not re-open
    // an editor for the same block — that would blank the just-committed text.
    int    m_lastCommittedPage { -1 };
    QRectF m_lastCommittedOrigBounds;

    QUndoStack *m_undoStack      { nullptr };

    DocumentHistory *m_history { nullptr };
    // What the file being opened stands for. Set by openWorkingCopy for the
    // moment openFile is inside it; Kind::Opened means "nothing special", i.e.
    // the user opened a document and the history starts over.
    DocumentHistory::Change m_openChange;
    // Restoring a state reopens files and moves the undo stack — none of which
    // is a change to record. Set while that is going on.
    bool m_restoring { false };
    // Set while an edit is being pushed onto the undo stack: the index moves,
    // but towards a state the history has not been told about yet.
    bool m_pushingEdit { false };

    OcrEngine *m_ocrEngine { nullptr };

#ifdef HAVE_PDF_RENDERING
    // Builds a session edit from the active editor state (font, colors,
    // form-field binding). Single source of truth for commit AND live paths.
    EditSession::Edit makeSessionEdit(int page, const QRectF &bounds,
                                      const QRectF &sourceRect,
                                      const QString &text) const;

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

    // Records that the session as it stands has been written to `path`, without
    // ending it: sets the clean marks hasUnsavedEdits() reads and re-announces
    // the document under the name the user saved it as.
    void markSaved(const QString &path);

    // Session state as of the last successful save, so a document that has
    // been saved does not keep claiming it has unsaved changes. Text edits are
    // measured by the undo stack's clean marker (which follows undo AND redo),
    // images by their revision counter.
    quint64 m_savedImageRevision { 0 };

    PdfRenderer  *m_renderer    { nullptr };
    EditSession  *m_session     { nullptr };
    TextBoxFrame *m_editorFrame { nullptr };
    // Content detection framework: per-page cached region model (text,
    // paragraphs, table cells, form fields, images, media).
    std::unique_ptr<ContentProvider> m_contentProvider;
    // Hover feedback for detected regions in edit mode.
    QFrame *m_hoverHighlight { nullptr };
    QRectF  m_hoverBounds;
    int     m_hoverPage { -1 };
    // Snapshot of session edits taken at editor-open time — "before" state for undo.
    QList<EditSession::Edit>              m_undoSnapBefore;
    QHash<int, QList<OcrEngine::Block>>   m_ocrCache;
#  ifdef HAVE_QT_PDF
    QPdfDocument     *m_document  { nullptr };
    PdfTextExtractor *m_extractor { nullptr };
#  elif defined(HAVE_POPPLER)
    std::unique_ptr<Poppler::Document> m_popplerDoc;
#  endif
#endif
};
