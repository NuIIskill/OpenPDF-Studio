#pragma once

#include <QWidget>
#include <QPixmap>

#ifdef HAVE_PDF_RENDERING
#  include <memory>
class PdfBackend;
#endif

QT_BEGIN_NAMESPACE
class QLabel;
class QTimer;
QT_END_NAMESPACE

class PresentationWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PresentationWindow(const QString &filePath, int startPage = 0,
                                QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    void renderCurrentPage();

    int     m_currentPage { 0 };
    int     m_pageCount   { 0 };
    QPixmap m_pagePixmap;

    QLabel *m_pageNum  { nullptr };
    QTimer *m_fadeTimer{ nullptr };

#ifdef HAVE_PDF_RENDERING

    std::unique_ptr<PdfBackend> m_backend;
#endif
};
