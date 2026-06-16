#pragma once

#include <QScrollArea>
#include <QUndoStack>

QT_BEGIN_NAMESPACE
class QLabel;
class QVBoxLayout;
class QRubberBand;
QT_END_NAMESPACE

#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#include "engine/view/PdfRenderer.hpp"
#include "engine/edit/PdfTextExtractor.hpp"
#include "engine/edit/EditSession.hpp"
#include "engine/edit/InlineEditor.hpp"
#include "engine/edit/TextBoxFrame.hpp"
#include "engine/edit/TextBlock.hpp"
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

    QString     currentFile()      const { return m_filePath; }
    int         pageCount()        const { return m_pageCount; }
    QUndoStack *undoStack()        const { return m_undoStack; }
    bool        hasUnsavedEdits()  const;
    bool        pdfRenderingAvailable() const;

Q_SIGNALS:
    void fileOpened(const QString &path, int pageCount);
    void pageChanged(int current, int total);

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

    // Edit-mode hit testing
    void   handleEditClick(const QPoint &canvasPos);
    void   commitCurrentEdit(const QString &newText);
    void   cancelCurrentEdit();

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

    // Edit-mode state
    int     m_activeEditPage { -1 };
    QRectF  m_activeEditBounds;
    QString m_activeEditOriginalText;

    QUndoStack *m_undoStack { nullptr };

#ifdef HAVE_QT_PDF
    QPdfDocument      *m_document    { nullptr };
    PdfRenderer       *m_renderer    { nullptr };
    PdfTextExtractor  *m_extractor   { nullptr };
    EditSession       *m_session     { nullptr };
    InlineEditor      *m_editor      { nullptr };
    TextBoxFrame      *m_editorFrame { nullptr };
#endif
};
