#include "ui/view/SmoothScroll.hpp"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QWindow>

#include <cmath>

namespace {

// The view closes the remaining distance with this time constant, so steps
// that follow each other add up to an even speed instead of a burst each.
constexpr double kTimeConstant = 0.045;

}

SmoothScroll::SmoothScroll(QAbstractScrollArea *area, QObject *parent)
    : QObject(parent)
    , m_area(area)
{
    m_vertical.bar   = area->verticalScrollBar();
    m_horizontal.bar = area->horizontalScrollBar();
    m_clock.start();
}

bool SmoothScroll::handleWheel(QWheelEvent *e)
{
    // A touchpad gesture comes in phases and follows the fingers; a mouse
    // wheel, notched or fine, glides, whether or not it also reports pixels.
    if (e->phase() != Qt::NoScrollPhase || e->source() != Qt::MouseEventNotSynthesized)
        return false;
    if (e->modifiers() & ~Qt::ShiftModifier) return false;
    QPoint delta = e->angleDelta();
    if (e->modifiers() & Qt::ShiftModifier) delta = QPoint(delta.y(), delta.x());
    if (delta.isNull()) return false;

    const int lines = QApplication::wheelScrollLines();
    if (delta.y() != 0 && m_vertical.bar)
        scrollBy(m_vertical, -delta.y() / 120.0 * lines * m_vertical.bar->singleStep());
    if (delta.x() != 0 && m_horizontal.bar)
        scrollBy(m_horizontal, -delta.x() / 120.0 * lines * m_horizontal.bar->singleStep());
    e->accept();
    return true;
}

void SmoothScroll::scrollBy(Axis &axis, double distance)
{
    const int value = axis.bar->value();
    if (!axis.active || axis.lastSet != value) {
        axis.position = value;
        axis.target   = value;
    }
    axis.target  = qBound(double(axis.bar->minimum()), axis.target + distance,
                          double(axis.bar->maximum()));
    axis.lastSet = value;
    axis.active  = qAbs(axis.target - axis.position) >= 0.5;
    if (axis.active) requestFrame();
}

void SmoothScroll::requestFrame()
{
    // Steps come with the display's frames, so every frame shows one.
    QWindow *window = m_area->window()->windowHandle();
    if (!window) return;
    if (window != m_window) {
        if (m_window) m_window->removeEventFilter(this);
        m_window = window;
        m_window->installEventFilter(this);
    }
    if (!m_waiting) {
        if (m_lastFrame == 0 || m_clock.elapsed() - m_lastFrame > 50) m_lastFrame = m_clock.elapsed();
        m_waiting = true;
        window->requestUpdate();
    }
}

bool SmoothScroll::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::UpdateRequest && m_waiting) {
        m_waiting = false;
        frame();
    }
    return QObject::eventFilter(watched, event);
}

void SmoothScroll::frame()
{
    const qint64 now = m_clock.elapsed();
    const double seconds = qBound(0.0, (now - m_lastFrame) / 1000.0, 0.05);
    m_lastFrame = now;
    const bool vertical   = advance(m_vertical, seconds);
    const bool horizontal = advance(m_horizontal, seconds);
    if (vertical || horizontal) requestFrame();
}

bool SmoothScroll::advance(Axis &axis, double seconds)
{
    if (!axis.active || !axis.bar) return false;
    // Anything else that moved the view, a zoom or a jump to a page, wins.
    if (axis.bar->value() != axis.lastSet) {
        axis.active = false;
        return false;
    }
    axis.position += (axis.target - axis.position) * (1.0 - std::exp(-seconds / kTimeConstant));
    if (qAbs(axis.target - axis.position) < 0.5) {
        axis.position = axis.target;
        axis.active   = false;
    }
    axis.bar->setValue(qRound(axis.position));
    axis.lastSet = axis.bar->value();
    return axis.active;
}
