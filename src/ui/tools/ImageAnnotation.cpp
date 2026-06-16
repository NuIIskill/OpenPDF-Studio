#include "ImageAnnotation.hpp"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QPainter>
#include <QPixmap>

ImageAnnotation::ImageAnnotation(const QString &imagePath, QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("ImageAnnotation"));
    setAttribute(Qt::WA_StyledBackground, false);
    setMinimumSize(MIN_W, MIN_H);
    setMouseTracking(true);

    QPixmap px(imagePath);
    m_origPixmap = px;

    int w = MIN_W, h = MIN_H;
    if (!px.isNull()) {
        const QSize bounded = px.size().boundedTo(QSize(MAX_W, MAX_H));
        w = bounded.width();
        h = bounded.height();
    }
    resize(w, h);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_imgLabel = new QLabel(this);
    m_imgLabel->setAlignment(Qt::AlignCenter);
    m_imgLabel->setScaledContents(true);
    if (!px.isNull())
        m_imgLabel->setPixmap(px);
    else
        m_imgLabel->setText(tr("(image not found)"));
    layout->addWidget(m_imgLabel);

    m_deleteBtn = new QPushButton(QStringLiteral("✕"), this);
    m_deleteBtn->setFixedSize(18, 18);
    m_deleteBtn->setFlat(true);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size:10px; color:#6B7280; border:none; background:rgba(255,255,255,180);"
        " border-radius:9px; }"
        "QPushButton:hover { color:#EF4444; background:rgba(255,255,255,230); }"));
    m_deleteBtn->hide();
    connect(m_deleteBtn, &QPushButton::clicked, this, &ImageAnnotation::deleteRequested);
}

void ImageAnnotation::setEditActive(bool on)
{
    m_editActive = on;
    if (!on) {
        m_deleteBtn->hide();
        m_hovered = false;
    }
    update();
}

void ImageAnnotation::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_editActive) {
        p.setPen(QPen(m_hovered ? QColor("#3B82F6") : QColor("#CBD5E1"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(0, 0, -1, -1));

        const QRect rh(width() - RESIZE_SZ - 2, height() - RESIZE_SZ - 2, RESIZE_SZ, RESIZE_SZ);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#3B82F6"));
        p.drawRoundedRect(rh, 2, 2);
    }
}

bool ImageAnnotation::isInResizeHandle(const QPoint &p) const
{
    return QRect(width() - RESIZE_SZ - 4, height() - RESIZE_SZ - 4,
                 RESIZE_SZ + 4, RESIZE_SZ + 4).contains(p);
}

void ImageAnnotation::enterEvent(QEnterEvent *e)
{
    QFrame::enterEvent(e);
    if (!m_editActive) return;
    m_hovered = true;
    m_deleteBtn->move(width() - 20, 2);
    m_deleteBtn->raise();
    m_deleteBtn->show();
    update();
}

void ImageAnnotation::leaveEvent(QEvent *e)
{
    QFrame::leaveEvent(e);
    m_hovered = false;
    m_deleteBtn->hide();
    update();
}

void ImageAnnotation::mousePressEvent(QMouseEvent *e)
{
    if (!m_editActive) { QFrame::mousePressEvent(e); return; }
    if (e->button() == Qt::LeftButton) {
        if (isInResizeHandle(e->pos())) {
            m_resizing     = true;
            m_resizeOrigin = e->globalPosition().toPoint();
            m_sizeAtResize = size();
            e->accept();
            return;
        }
        m_dragging      = true;
        m_posBeforeDrag = pos();
        m_dragOff       = e->pos();
        e->accept();
        return;
    }
    QFrame::mousePressEvent(e);
}

void ImageAnnotation::mouseMoveEvent(QMouseEvent *e)
{
    if (m_resizing) {
        const QPoint delta = e->globalPosition().toPoint() - m_resizeOrigin;
        resize(qBound(MIN_W, m_sizeAtResize.width()  + delta.x(), MAX_W),
               qBound(MIN_H, m_sizeAtResize.height() + delta.y(), MAX_H));
        m_deleteBtn->move(width() - 20, 2);
        e->accept();
        return;
    }
    if (m_dragging) {
        move(mapToParent(e->pos()) - m_dragOff);
        e->accept();
        return;
    }
    if (m_editActive && isInResizeHandle(e->pos()))
        setCursor(Qt::SizeFDiagCursor);
    else
        setCursor(m_editActive ? Qt::SizeAllCursor : Qt::ArrowCursor);
    QFrame::mouseMoveEvent(e);
}

void ImageAnnotation::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_resizing) { m_resizing = false; update(); e->accept(); return; }
    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        if (pos() != m_posBeforeDrag)
            Q_EMIT moved(m_posBeforeDrag, pos());
        e->accept();
        return;
    }
    QFrame::mouseReleaseEvent(e);
}
