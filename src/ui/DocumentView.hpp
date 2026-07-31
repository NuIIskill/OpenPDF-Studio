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
QT_END_NAMESPACE

class ImageAnnotationLayer;
class PageLayoutEngine;
class TextSelectionController;

#include "engine/ocr/OcrEngine.hpp"
#include "engine/edit/DocxExporter.hpp"
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

    QString     currentFile()      const { return m_filePath; }
    int         pageCount()        const override { return m_pageCount; }
    // 0-based index of the page the user is looking at.
    int         currentPage()      const;
    // Scrolls the given 0-based page to the top of the viewport.
    void        goToPage(int page);
    ViewMode    viewMode()         const { return m_viewMode; }
    QUndoStack *undoStack()        const { return m_undoStack; }
    bool        hasUnsavedEdits()  const;
    bool        pdfRenderingAvailable() const;
    QList<DocxPage> allPageContent();
    bool exportPagesToImages(const QString &outputPath, int quality = 85);
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
#ifdef HAVE_POPPLER
    bool savePopplerRaster(const QString &outputPath);
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
    QString m_filePath;
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
    QString m_activeEditOriginalText;
    QString m_activeEditFieldName;   // non-empty: editing an AcroForm text field
    int     m_currentEditorFontSizePt { 12 };
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

    OcrEngine *m_ocrEngine { nullptr };

#ifdef HAVE_PDF_RENDERING
    // Builds a session edit from the active editor state (font, colors,
    // form-field binding). Single source of truth for commit AND live paths.
    EditSession::Edit makeSessionEdit(int page, const QRectF &bounds,
                                      const QRectF &sourceRect,
                                      const QString &text) const;

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
