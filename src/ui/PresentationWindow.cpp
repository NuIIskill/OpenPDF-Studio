#include "PresentationWindow.hpp"

#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QLabel>
#include <QTimer>

#ifdef HAVE_QT_PDF
#  include <QPdfDocument>
#endif

PresentationWindow::PresentationWindow(const QString &filePath, int startPage, QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Präsentation"));
    setStyleSheet(QStringLiteral("background: black;"));
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::BlankCursor);

    m_pageNum = new QLabel(this);
    m_pageNum->setAlignment(Qt::AlignCenter);
    m_pageNum->setStyleSheet(QStringLiteral(
        "color: white; background: rgba(0,0,0,160);"
        " border-radius: 6px; padding: 4px 12px; font-size: 14px;"));
    m_pageNum->hide();

    m_fadeTimer = new QTimer(this);
    m_fadeTimer->setSingleShot(true);
    m_fadeTimer->setInterval(2000);
    connect(m_fadeTimer, &QTimer::timeout, m_pageNum, &QLabel::hide);

#ifdef HAVE_QT_PDF
    m_doc = new QPdfDocument(this);
    if (m_doc->load(filePath) == QPdfDocument::Error::None)
        m_pageCount = m_doc->pageCount();
#endif

    m_currentPage = qBound(0, startPage, qMax(0, m_pageCount - 1));
    showFullScreen();
}

void PresentationWindow::renderCurrentPage()
{
#ifdef HAVE_QT_PDF
    if (!m_doc || m_pageCount == 0) return;

    const QSizeF pagePts  = m_doc->pagePointSize(m_currentPage);
    const QSize  winSz    = size().isEmpty() ? QSize(1920, 1080) : size();
    const qreal  scale    = qMin(winSz.width()  / pagePts.width(),
                                 winSz.height() / pagePts.height());
    const QSize  renderSz(qRound(pagePts.width()  * scale),
                          qRound(pagePts.height() * scale));

    m_pagePixmap = QPixmap::fromImage(m_doc->render(m_currentPage, renderSz));
#endif
    update();

    if (m_pageCount > 0) {
        m_pageNum->setText(QStringLiteral("%1 / %2")
                           .arg(m_currentPage + 1).arg(m_pageCount));
        m_pageNum->adjustSize();
        m_pageNum->move((width()  - m_pageNum->width())  / 2,
                        height() - m_pageNum->height() - 24);
        m_pageNum->show();
        m_pageNum->raise();
        m_fadeTimer->start();
    }
}

void PresentationWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!m_pagePixmap.isNull()) {
        const int x = (width()  - m_pagePixmap.width())  / 2;
        const int y = (height() - m_pagePixmap.height()) / 2;
        p.drawPixmap(x, y, m_pagePixmap);
    }
}

void PresentationWindow::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_PageDown:
        if (m_currentPage < m_pageCount - 1) { ++m_currentPage; renderCurrentPage(); }
        break;
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_Backspace:
    case Qt::Key_PageUp:
        if (m_currentPage > 0) { --m_currentPage; renderCurrentPage(); }
        break;
    case Qt::Key_Home:
        if (m_currentPage != 0) { m_currentPage = 0; renderCurrentPage(); }
        break;
    case Qt::Key_End:
        if (m_currentPage != m_pageCount - 1) { m_currentPage = m_pageCount - 1; renderCurrentPage(); }
        break;
    case Qt::Key_Escape:
    case Qt::Key_F5:
        close();
        break;
    default:
        QWidget::keyPressEvent(e);
    }
}

void PresentationWindow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        if (m_currentPage < m_pageCount - 1) { ++m_currentPage; renderCurrentPage(); }
    } else if (e->button() == Qt::RightButton) {
        if (m_currentPage > 0) { --m_currentPage; renderCurrentPage(); }
    }
}

void PresentationWindow::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    renderCurrentPage();
}
