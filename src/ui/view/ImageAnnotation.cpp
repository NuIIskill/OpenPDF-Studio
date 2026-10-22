#include "ui/view/ImageAnnotation.hpp"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QContextMenuEvent>

#include <climits>

ImageAnnotation::ImageAnnotation(const QString &imagePath, QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("ImageAnnotation"));
    setAttribute(Qt::WA_StyledBackground, false);
    setMinimumSize(MIN_W, MIN_H);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);

    m_imgLabel = new QLabel(this);
    m_imgLabel->setAlignment(Qt::AlignCenter);
    m_imgLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    // The page render already shows the image; the label only follows a drag or resize.
    m_imgLabel->hide();
    layout->addWidget(m_imgLabel);

    m_deleteBtn = new QPushButton(QStringLiteral("✕"), this);
    m_deleteBtn->setFixedSize(20, 20);
    m_deleteBtn->setFlat(true);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size:11px; color:#6B7280; border:none;"
        " background:rgba(255,255,255,200); border-radius:10px; }"
        "QPushButton:hover { color:#EF4444; background:rgba(255,255,255,240); }"));
    m_deleteBtn->hide();
    connect(m_deleteBtn, &QPushButton::clicked, this, &ImageAnnotation::deleteRequested);

    if (!imagePath.isEmpty()) {
        const QPixmap px(imagePath);
        if (!px.isNull()) setOriginalPixmap(px);
    }
}

void ImageAnnotation::setOriginalPixmap(const QPixmap &px)
{
    m_origPixmap = px;
    updateScaledPixmap();
}

void ImageAnnotation::updateScaledPixmap()
{
    if (m_origPixmap.isNull()) return;
    const QSize inner(width() - 4, height() - 4);
    if (inner.isEmpty()) return;
    m_imgLabel->setPixmap(m_origPixmap.scaled(inner, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ImageAnnotation::resizeEvent(QResizeEvent *e)
{
    QFrame::resizeEvent(e);
    updateScaledPixmap();
    if (m_hovered) m_deleteBtn->move(width() - 22, 2);
}

void ImageAnnotation::setEditActive(bool on)
{
    m_editActive = on;
    if (!on) { m_deleteBtn->hide(); m_hovered = false; }
    update();
}

void ImageAnnotation::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_editActive) {
        p.setPen(QPen(m_hovered ? QColor("#3B82F6") : QColor("#94A3B8"), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(1, 1, -2, -2));

        const QRect rh(width() - RESIZE_SZ - 2, height() - RESIZE_SZ - 2,
                       RESIZE_SZ, RESIZE_SZ);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#3B82F6"));
        p.drawRoundedRect(rh, 3, 3);
    }
}

bool ImageAnnotation::isInResizeHandle(const QPoint &pt) const
{
    return QRect(width() - RESIZE_SZ - 6, height() - RESIZE_SZ - 6,
                 RESIZE_SZ + 6, RESIZE_SZ + 6).contains(pt);
}

void ImageAnnotation::enterEvent(QEnterEvent *e)
{
    QFrame::enterEvent(e);
    if (!m_editActive) return;
    m_hovered = true;
    m_deleteBtn->move(width() - 22, 2);
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
            m_imgLabel->show();
            setCursor(Qt::SizeFDiagCursor);
            grabMouse(Qt::SizeFDiagCursor);
            e->accept();
            return;
        }
        m_dragging      = true;
        m_posBeforeDrag = pos();
        m_dragOff       = e->pos();
        m_imgLabel->show();
        setCursor(Qt::SizeAllCursor);
        grabMouse(Qt::SizeAllCursor);
        e->accept();
        return;
    }
    QFrame::mousePressEvent(e);
}

void ImageAnnotation::mouseMoveEvent(QMouseEvent *e)
{
    if (m_resizing) {
        const QPoint delta = e->globalPosition().toPoint() - m_resizeOrigin;
        const int maxW = m_pageRect.isEmpty() ? INT_MAX : m_pageRect.right()  - pos().x();
        const int maxH = m_pageRect.isEmpty() ? INT_MAX : m_pageRect.bottom() - pos().y();
        int newW = qMin(qMax(MIN_W, m_sizeAtResize.width()  + delta.x()), maxW);
        int newH = qMin(qMax(MIN_H, m_sizeAtResize.height() + delta.y()), maxH);
        if ((e->modifiers() & Qt::ShiftModifier) && !m_sizeAtResize.isEmpty()) {
            const qreal w0 = m_sizeAtResize.width();
            const qreal h0 = m_sizeAtResize.height();
            qreal f = qMax((w0 + delta.x()) / w0, (h0 + delta.y()) / h0);
            f = qMax(f, qMax(MIN_W / w0, MIN_H / h0));
            f = qMin(f, qMin(maxW / w0, maxH / h0));
            newW = qRound(w0 * f);
            newH = qRound(h0 * f);
        }
        resize(newW, newH);
        if (m_hovered) m_deleteBtn->move(width() - 22, 2);
        e->accept();
        return;
    }
    if (m_dragging) {
        QPoint newPos = mapToParent(e->pos()) - m_dragOff;
        if (e->modifiers() & Qt::ShiftModifier) {
            const QPoint d = newPos - m_posBeforeDrag;
            if (qAbs(d.x()) >= qAbs(d.y())) newPos.setY(m_posBeforeDrag.y());
            else                             newPos.setX(m_posBeforeDrag.x());
        }
        if (!m_pageRect.isEmpty()) {
            newPos.setX(qBound(m_pageRect.left(), newPos.x(), m_pageRect.right()  - width()));
            newPos.setY(qBound(m_pageRect.top(),  newPos.y(), m_pageRect.bottom() - height()));
        }
        move(newPos);
        e->accept();
        return;
    }
    if (m_editActive)
        setCursor(isInResizeHandle(e->pos()) ? Qt::SizeFDiagCursor : Qt::SizeAllCursor);
    QFrame::mouseMoveEvent(e);
}

void ImageAnnotation::contextMenuEvent(QContextMenuEvent *e)
{
    if (m_editActive)
        Q_EMIT contextMenuRequested(e->globalPos());
    e->accept();
}

void ImageAnnotation::keyPressEvent(QKeyEvent *e)
{
    if (!m_editActive || m_dragging || m_resizing) { QFrame::keyPressEvent(e); return; }
    if (e->matches(QKeySequence::Copy))     Q_EMIT copyRequested();
    else if (e->matches(QKeySequence::Cut)) Q_EMIT cutRequested();
    else if (e->matches(QKeySequence::Paste)) Q_EMIT pasteRequested();
    else if (e->key() == Qt::Key_Delete)    Q_EMIT deleteRequested();
    else { QFrame::keyPressEvent(e); return; }
    e->accept();
}

void ImageAnnotation::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && (m_resizing || m_dragging)) {
        m_resizing = m_dragging = false;
        m_imgLabel->hide();
        releaseMouse();
        setCursor(Qt::SizeAllCursor);
        Q_EMIT geometryChanged(geometry());
        e->accept();
        return;
    }
    QFrame::mouseReleaseEvent(e);
}
