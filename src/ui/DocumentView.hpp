#pragma once

#include <functional>

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

    bool   openWorkingCopy(const QString &contentPath, const QString &targetPath,
                           const DocumentHistory::Change &change = {});
    void   clearDocument();
    void   setZoom(int percent);
    void   setZoomSettings(int step, bool ctrlWheel, bool toPointer,
                           const QString &wheelAction);
    void   setTool(Tool tool);

    void   setActiveToolId(const QString &toolId);
    void   setEditMode(bool on);
    void   setViewMode(ViewMode mode);
    bool   saveToFile(const QString &path);
    void   retranslateUi();
    void   refreshTheme();

    void   setEditorFontSize(int ptSize);

    void   setEditorTextColor(const QColor &color);

    void   setEditorFontFamily(const QString &family);
    void   setEditorBold(bool on);
    void   setEditorItalic(bool on);
    void   setEditorUnderline(bool on);
    void   setTextBoxProperties(const TextBoxProperties &properties);
    void   setTextBoxDefaults(const TextBoxProperties &properties);
    void   setEditorAlignment(Qt::Alignment alignment);
    void   setEditorListStyle(TextBoxProperties::ListStyle style);
    void   changeEditorIndent(int delta);
    void   setEditorLineSpacing(double multiplier);
    void   setDrawTool(DrawTool tool);
    void   setDrawColor(const QColor &color);
    void   setDrawWidth(qreal widthPt);

    QString     currentFile()      const
    {
        if (m_journal.workingCopyDirty && m_journal.targetPath.isEmpty()) return {};
        return m_journal.targetPath.isEmpty() ? m_src->contentPath() : m_journal.targetPath;
    }

    QString     contentFile()      const { return m_src->contentPath(); }
    int         pageCount()        const override { return m_src->pageCount(); }

    int         currentPage()      const;

    void        goToPage(int page);
    const QList<PdfBookmark> &bookmarks() const { return m_bookmarks; }
    void        setBookmarks(const QList<PdfBookmark> &bookmarks);
    bool        bookmarkEditingAvailable() const;
    ViewMode    viewMode()         const { return m_viewMode; }
    QUndoStack *undoStack()        const { return m_undoStack; }

    void        undo();
    void        redo();
    QRectF editBounds() const;
    QRectF editFrameRect() const;
    double editFontSizePt() const;

    DocumentHistory *history()     const { return m_journal.history(); }

    bool        restoreHistoryState(int index);
    bool        hasUnsavedEdits()  const
    { return m_journal.hasUnsavedEdits() || m_bookmarksDirty; }
    bool        pdfRenderingAvailable() const;
    QList<DocxPage> allPageContent(const QList<int> &pages = {});
    bool exportPagesToImages(const QString &outputPath, int quality = 85,
                             const QList<int> &pages = {});
#ifdef HAVE_QT_PRINT

    bool printDocument(QPrinter *printer, const QList<int> &pages = {});
#endif

    void        rerenderPage(int page);

    QString     selectedText() const;
    void        copySelectedText();
    void        openFind();
    QList<NoteData> notes() const;
    void createNote();
    void selectNote(const QString &id);
    void updateNote(const QString &id, const QString &title, const QString &text);
    void deleteNote(const QString &id);
    void setNotePinned(const QString &id, bool pinned);

    QWidget *canvasWidget()   const override { return m_canvas; }
    QLabel  *pageLabel(int page) const override;
    int      pageLabelCount() const override;
    qreal    screenScale()    const override;
    std::pair<int, QLabel *> pageAtCanvasPos(const QPoint &canvasPos) const override;

Q_SIGNALS:
    void fileOpened(const QString &path, int pageCount);

    void pdfDropped(const QString &path);
    void pageChanged(int current, int total);
    void viewModeChanged(ViewMode mode);
    void zoomChanged(int percent);

    void editorFontSizeChanged(int ptSize);

    void editorFontChanged(const QString &family, bool bold, bool italic,
                           bool underline);
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

    void   rerenderPageWithBlank(int page, const QRectF &pdfBoundsPts);

    QRect  visibleCanvasRect() const;
    void   syncVisibleRect();

    void   repositionForZoom();
    void   repositionEditorFrame();

    void   repositionPageOverlays();
    void   scrollToSearchMatch(int page, const QRectF &bounds);

#ifdef HAVE_PDF_RENDERING

    DocumentExporter::Sources exportSources() const;
#endif

    void   showGeneralContextMenu(const QPoint &globalPos);

    int    firstVisiblePage() const;

    void   handleEditClick(const QPoint &canvasPos);
    void   createTextFrame(const QRect &viewportDragRect);
    void   closeEditorBeforeUndo();

    void   keepScroll(const std::function<void()> &schliessen);
    void   commitCurrentEdit(const QString &newText);
    void   cancelCurrentEdit();

#ifdef HAVE_PDF_RENDERING

    struct EditOpen;

    bool   resolveEditTarget(const QPoint &canvasPos, EditOpen &o);

    void   applyEditTargetBounds(EditOpen &o);

    void   chooseEditorFont(EditOpen &o);

    void   anchorEditOrigin(EditOpen &o);

    void   fitEditHeight(EditOpen &o);

    void   sampleEditColors(EditOpen &o);

    void   fitEditWidth(EditOpen &o);

    void   presentEditor(EditOpen &o);
#endif

    DocumentSource::PasswordAsker askPassword();

    void   resetContentProvider();

    void   refreshEditorFontLive();

    void reportCurrentPage();
    void scrollToPage(int page, bool allowRetry);

    QList<DocumentHistory::ImageState> imageStates() const;

    QWidget     *m_canvas    { nullptr };
    QVBoxLayout *m_layout    { nullptr };
    QLabel      *m_dropHint  { nullptr };
    QRubberBand *m_rubberBand{ nullptr };

    PageLayoutEngine *m_layoutEngine { nullptr };

    ImageAnnotationLayer *m_imageLayer { nullptr };
    LinkAnnotationLayer  *m_linkLayer  { nullptr };
    NoteLayer            *m_noteLayer  { nullptr };
    DrawingLayer         *m_drawingLayer { nullptr };

    HoverHighlight *m_hover { nullptr };

    QList<PageOverlay *> m_overlays;

    ViewMode  m_viewMode   { ViewMode::Single };
    QWidget  *m_gridCanvas { nullptr };

    std::unique_ptr<DocumentSource> m_src;

    DocumentJournal m_journal;

    QList<PdfBookmark> m_bookmarks;
    bool               m_bookmarksDirty { false };

    int     m_lastReportedPage { -1 };
    Tool    m_tool      { Tool::Select };
    bool    m_editMode  { false };

    ZoomController *m_zoomCtl { nullptr };

    QPoint m_panStart;
    QPoint m_panScrollOrigin;

    TextSelectionController *m_selection { nullptr };

    FindController *m_find { nullptr };

    bool   m_textTracking { false };
    bool   m_textDragging { false };
    QPoint m_textDragStart;

    EditController m_edit;

    QUndoStack *m_undoStack      { nullptr };

    OcrEngine *m_ocrEngine { nullptr };

#ifdef HAVE_PDF_RENDERING

    void discardEditHistory();

    bool detachSourceFrom(const QString &saveTarget);

    EditSession  *m_session     { nullptr };
    TextBoxFrame *m_editorFrame { nullptr };
#endif
};
