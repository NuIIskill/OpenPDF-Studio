#pragma once

#include <QScrollArea>
#include <QUndoStack>

QT_BEGIN_NAMESPACE
class QLabel;
class QVBoxLayout;
class QRubberBand;
QT_END_NAMESPACE

#include "engine/ocr/OcrEngine.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/view/PdfRenderer.hpp"
#  ifdef HAVE_QT_PDF
#    include <QPdfDocument>
#    include <QHash>
#    include "engine/edit/PdfTextExtractor.hpp"
#    include "engine/edit/EditSession.hpp"
#    include "engine/edit/TextBoxFrame.hpp"
#    include "engine/edit/TextBlock.hpp"
#  elif defined(HAVE_POPPLER)
#    include <memory>
#    include <poppler/qt6/poppler-qt6.h>
#  endif
#endif

class DocumentView : public QScrollArea
{
    Q_OBJECT

public:
    enum class Tool { Select, Pan, Text, Comment, Image };

    explicit DocumentView(QWidget *parent = nullptr);
    ~DocumentView() override;

    bool   openFile(const QString &path);
    void   clearDocument();
    void   setZoom(int percent);
    void   setTool(Tool tool);
    void   setEditMode(bool on);
    bool   saveToFile(const QString &path);
    void   retranslateUi();
    // Called by MainWindow when the user changes the font size in the FormatBar.
    void   setEditorFontSize(int ptSize);

    QString     currentFile()      const { return m_filePath; }
    int         pageCount()        const { return m_pageCount; }
    QUndoStack *undoStack()        const { return m_undoStack; }
    bool        hasUnsavedEdits()  const;
    bool        pdfRenderingAvailable() const;
    QList<QString> allPageTexts() const;

Q_SIGNALS:
    void fileOpened(const QString &path, int pageCount);
    void pageChanged(int current, int total);
    // Emitted when an editor opens so the FormatBar can show the correct font size.
    void editorFontSizeChanged(int ptSize);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    // Page rendering
    void   buildPages();
    void   rerenderAll();
    void   rerenderPage(int page);
    void   rerenderPageWithBlank(int page, const QRectF &pdfBoundsPts);

    // Edit-mode interaction
    void   handleEditClick(const QPoint &canvasPos);
    void   createTextFrame(const QRect &viewportDragRect);
    void   commitCurrentEdit(const QString &newText);
    void   cancelCurrentEdit();
    void   liveUpdateCurrentEdit(const QString &text);

    // Canvas helpers
    std::pair<int, QLabel *> pageAtCanvasPos(const QPoint &canvasPos) const;

    // Widgets
    QWidget     *m_canvas    { nullptr };
    QVBoxLayout *m_layout    { nullptr };
    QLabel      *m_dropHint  { nullptr };
    QRubberBand *m_rubberBand{ nullptr };
    QList<QLabel *> m_pageLabels;

    // Document state
    QString m_filePath;
    int     m_zoom      { 100 };
    int     m_pageCount { 0 };
    Tool    m_tool      { Tool::Select };
    bool    m_editMode  { false };

    // View-mode interaction
    QPoint m_panStart;
    QPoint m_panScrollOrigin;
    QPoint m_selectStart;

    // Text-tool drag-to-create state (viewport coords)
    bool   m_textTracking { false };
    bool   m_textDragging { false };
    QPoint m_textDragStart;

    // Edit-mode state
    int     m_activeEditPage { -1 };
    QRectF  m_activeEditBounds;
    QRectF  m_activeEditOriginalBounds;  // bounds when the edit was first opened
    QRectF  m_lastLiveEditBounds;        // bounds where the last live edit was placed
    bool    m_hasLiveEdit     { false }; // whether a live edit is currently in the session
    QString m_activeEditOriginalText;
    int     m_currentEditorFontSizePt { 12 };  // tracks font size set via FormatBar
    QColor  m_currentEditorColor     { 0x11, 0x11, 0x11 };  // sampled from page render

    // After a commit, the press+release that triggered it must not re-open
    // an editor for the same block — that would blank the just-committed text.
    int    m_lastCommittedPage { -1 };
    QRectF m_lastCommittedOrigBounds;

    QUndoStack *m_undoStack      { nullptr };
    QTimer     *m_liveTimer      { nullptr };
    QString     m_livePendingText;

    OcrEngine *m_ocrEngine { nullptr };

#ifdef HAVE_PDF_RENDERING
    PdfRenderer *m_renderer { nullptr };
#  ifdef HAVE_QT_PDF
    QPdfDocument      *m_document    { nullptr };
    PdfTextExtractor  *m_extractor   { nullptr };
    EditSession       *m_session     { nullptr };
    TextBoxFrame      *m_editorFrame { nullptr };
    QHash<int, QList<OcrEngine::Block>> m_ocrCache;
#  elif defined(HAVE_POPPLER)
    std::unique_ptr<Poppler::Document> m_popplerDoc;
#  endif
#endif
};
