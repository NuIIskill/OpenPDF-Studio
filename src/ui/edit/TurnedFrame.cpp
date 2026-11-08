#include "ui/edit/TurnedFrame.hpp"

#include "ui/edit/InlineEditor.hpp"
#include "ui/edit/TextBoxFrame.hpp"

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>
#include <utility>

namespace {

// A resize cursor turned a quarter turn points the other way.
Qt::CursorShape turnedShape(Qt::CursorShape shape, double angle)
{
    if (std::lround(angle / 90.0) % 2 == 0) return shape;
    switch (shape) {
    case Qt::SizeHorCursor:   return Qt::SizeVerCursor;
    case Qt::SizeVerCursor:   return Qt::SizeHorCursor;
    case Qt::SizeFDiagCursor: return Qt::SizeBDiagCursor;
    case Qt::SizeBDiagCursor: return Qt::SizeFDiagCursor;
    default:                  return shape;
    }
}

}

TurnedFrame::TurnedFrame(TextBoxFrame *frame)
    : QWidget(frame->parentWidget())
    , m_frame(frame)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    frame->installEventFilter(this);
    hide();
}

void TurnedFrame::place(const QTransform &turn, double angle)
{
    m_turn  = turn;
    m_angle = angle;
    follow();
    raise();
    show();
}

void TurnedFrame::leave()
{
    m_held.clear();
    m_dragging = false;
    hide();
    if (m_frame->paintHidden()) m_frame->setPaintHidden(false);
}

QTransform TurnedFrame::toCanvas() const
{
    return QTransform::fromTranslate(m_frame->x(), m_frame->y()) * m_turn;
}

void TurnedFrame::follow()
{
    const QRect area = toCanvas().mapRect(QRectF(m_frame->rect())).toAlignedRect();
    if (area != geometry()) setGeometry(area);
    // Where the window lies over the text, the window is drawn and takes the pointer.
    const QRegion free = QRegion(rect()) - QRegion(m_frame->geometry().translated(-pos()));
    setMask(free.isEmpty() ? QRegion(-1, -1, 1, 1) : free);
    update();
}

bool TurnedFrame::eventFilter(QObject *watched, QEvent *event)
{
    if (isVisible() && !m_frame->renderingTurned()) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
            if (watched == m_frame) follow();
            break;
        case QEvent::Paint:
            update();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TurnedFrame::paintEvent(QPaintEvent *)
{
    const qreal dpr = devicePixelRatioF();
    QImage frame(m_frame->size() * dpr, QImage::Format_ARGB32_Premultiplied);
    frame.setDevicePixelRatio(dpr);
    frame.fill(Qt::transparent);
    m_frame->renderTurned(&frame);
    QPainter p(this);
    p.setTransform(toCanvas() * QTransform::fromTranslate(-x(), -y()));
    p.drawImage(QPointF(), frame);
}

void TurnedFrame::pass(QMouseEvent *event)
{
    // The spot under the pointer, turned back into the upright frame.
    const QPointF local = toCanvas().inverted().map(event->position() + QPointF(pos()));
    QWidget *editorView = m_frame->editor()->viewport();
    QWidget *target = m_held;
    if (!target) {
        const QPointF inEditor = editorView->mapFrom(m_frame, local);
        target = editorView->rect().contains(inEditor.toPoint()) ? editorView : m_frame;
    }
    const QPointF at = target->mapFrom(m_frame, local);
    const bool press = event->type() == QEvent::MouseButtonPress;
    if (press && target == m_frame && m_onDrag) m_onDrag(true);
    QMouseEvent turned(event->type(), at, at, target->mapToGlobal(at), event->button(),
                       event->buttons(), event->modifiers(), event->source());
    QApplication::sendEvent(target, &turned);
    if (press) {
        // This frame takes no focus, so the click would hand the keys to the page behind it.
        m_frame->editor()->setFocus(Qt::MouseFocusReason);
        m_held = target;
        m_dragging = target == m_frame && m_frame->dragging();
        if (target == m_frame && !m_dragging && m_onDrag) m_onDrag(false);
    }
    setCursor(turnedShape(target->cursor().shape(), m_angle));
    event->accept();
}

void TurnedFrame::mousePressEvent(QMouseEvent *event)          { pass(event); }
void TurnedFrame::mouseMoveEvent(QMouseEvent *event)           { pass(event); }
void TurnedFrame::mouseDoubleClickEvent(QMouseEvent *event)    { pass(event); }

void TurnedFrame::mouseReleaseEvent(QMouseEvent *event)
{
    pass(event);
    if (event->buttons()) return;
    m_held.clear();
    if (std::exchange(m_dragging, false) && m_onDrag) m_onDrag(false);
}
