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
#endif

class DocumentView : public QScrollArea
{
    Q_OBJECT

public:
    enum class Tool { Select, Pan, Text, Comment, Image };

    explicit DocumentView(QWidget *parent = nullptr);
    ~DocumentView() override;

    bool   openFile(const QString &path);
    void   setZoom(int percent);
    void   setTool(Tool tool);
    void   retranslateUi();

    QString     currentFile() const { return m_filePath; }
    int         pageCount()   const { return m_pageCount; }
    QUndoStack *undoStack()   const { return m_undoStack; }

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
    void   buildPages();
    void   rerenderAll();
    QImage renderPage(int index);
    QSize  pageDisplaySize() const;
    void   copySelectionToClipboard(const QRect &viewportRect);
    void   placeTextAnnotation(const QPoint &canvasPos);
    void   placeCommentAnnotation(const QPoint &canvasPos);
    void   placeImageAnnotation(const QPoint &canvasPos);

    QWidget     *m_canvas    { nullptr };
    QVBoxLayout *m_layout    { nullptr };
    QLabel      *m_dropHint  { nullptr };
    QRubberBand *m_rubberBand{ nullptr };

    QList<QLabel *> m_pageLabels;

    QString m_filePath;
    int     m_zoom      { 100 };
    int     m_pageCount { 0 };
    Tool    m_tool      { Tool::Select };

    QPoint m_panStart;
    QPoint m_panScrollOrigin;
    QPoint m_selectStart;

    QUndoStack *m_undoStack { nullptr };

#ifdef HAVE_QT_PDF
    QPdfDocument *m_document { nullptr };
#endif
};
