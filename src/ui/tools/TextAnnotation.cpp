#include "ui/tools/TextAnnotation.hpp"

#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QPainter>
#include <QApplication>
#include <QFontMetrics>

TextAnnotation::TextAnnotation(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("TextAnnotation"));
    setAttribute(Qt::WA_StyledBackground, false);
    setMinimumSize(MIN_W, MIN_H);
    resize(220, 80);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(0);

    m_edit = new QTextEdit(this);
    m_edit->setPlaceholderText(tr("Enter text…"));
    m_edit->setFrameShape(QFrame::NoFrame);
    m_edit->setStyleSheet(QStringLiteral(
        "QTextEdit { background: transparent; border: none; font-size: 13px; color: #111; }"));
    m_edit->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    layout->addWidget(m_edit);

    m_deleteBtn = new QPushButton(QStringLiteral("✕"), this);
    m_deleteBtn->setFixedSize(18, 18);
    m_deleteBtn->setFlat(true);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size:10px; color:#6B7280; border:none; background:rgba(255,255,255,180);"
        " border-radius:9px; }"
        "QPushButton:hover { color:#EF4444; background:rgba(255,255,255,230); }"));
    m_deleteBtn->hide();
    connect(m_deleteBtn, &QPushButton::clicked, this, &TextAnnotation::deleteRequested);

    setMouseTracking(true);
    m_edit->setFocus();
}

void TextAnnotation::setEditActive(bool on)
{
    m_editActive = on;
    m_edit->setReadOnly(!on);
    if (!on) {
        m_deleteBtn->hide();
        m_hovered = false;
    }
    update();
}

void TextAnnotation::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 240));
    p.drawRoundedRect(rect(), 3, 3);

    if (m_editActive) {

        const bool focused = m_edit->hasFocus() || m_hovered;
        p.setPen(QPen(focused ? QColor("#3B82F6") : QColor("#CBD5E1"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 3, 3);

        const QRect rh(width() - RESIZE_SZ - 2, height() - RESIZE_SZ - 2, RESIZE_SZ, RESIZE_SZ);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#3B82F6"));
        p.drawRoundedRect(rh, 2, 2);
    }
}

bool TextAnnotation::isInResizeHandle(const QPoint &p) const
{
    return QRect(width() - RESIZE_SZ - 4, height() - RESIZE_SZ - 4,
                 RESIZE_SZ + 4, RESIZE_SZ + 4).contains(p);
}

void TextAnnotation::enterEvent(QEnterEvent *e)
{
    QFrame::enterEvent(e);
    if (!m_editActive) return;
    m_hovered = true;
    m_deleteBtn->move(width() - 20, 2);
    m_deleteBtn->raise();
    m_deleteBtn->show();
    update();
}

void TextAnnotation::leaveEvent(QEvent *e)
{
    QFrame::leaveEvent(e);
    m_hovered = false;
    if (!m_edit->hasFocus())
        m_deleteBtn->hide();
    update();
}

void TextAnnotation::mousePressEvent(QMouseEvent *e)
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

        if (!m_edit->geometry().contains(e->pos())) {
            m_dragging     = true;
            m_posBeforeDrag = pos();
            m_dragOff       = e->pos();
            e->accept();
            return;
        }
    }
    QFrame::mousePressEvent(e);
}

void TextAnnotation::mouseMoveEvent(QMouseEvent *e)
{
    if (m_resizing) {
        const QPoint delta = e->globalPosition().toPoint() - m_resizeOrigin;
        resize(qMax(MIN_W, m_sizeAtResize.width()  + delta.x()),
               qMax(MIN_H, m_sizeAtResize.height() + delta.y()));
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
    else if (!m_edit->geometry().contains(e->pos()))
        setCursor(m_editActive ? Qt::SizeAllCursor : Qt::ArrowCursor);
    else
        setCursor(Qt::IBeamCursor);
    QFrame::mouseMoveEvent(e);
}

void TextAnnotation::mouseReleaseEvent(QMouseEvent *e)
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
