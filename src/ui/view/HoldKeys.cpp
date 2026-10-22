#include "ui/view/HoldKeys.hpp"

#include <QAbstractScrollArea>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>

HoldKeys::HoldKeys(QAbstractScrollArea *area)
    : QObject(area)
    , m_area(area)
{
    area->installEventFilter(this);
}

bool HoldKeys::handleMouse(QMouseEvent *e, const QPoint &viewportPos)
{
    if (!m_space && !m_panTool) return false;

    switch (e->type()) {
    case QEvent::MouseButtonPress:
        if (e->button() != Qt::LeftButton) return m_space;
        m_area->setFocus(Qt::MouseFocusReason);
        m_panning      = true;
        m_panStart     = viewportPos;
        m_scrollOrigin = { m_area->horizontalScrollBar()->value(),
                           m_area->verticalScrollBar()->value() };
        m_area->viewport()->setCursor(Qt::ClosedHandCursor);
        return true;
    case QEvent::MouseMove:
        if (m_panning && (e->buttons() & Qt::LeftButton)) {
            const QPoint d = viewportPos - m_panStart;
            m_area->horizontalScrollBar()->setValue(m_scrollOrigin.x() - d.x());
            m_area->verticalScrollBar()->setValue(m_scrollOrigin.y() - d.y());
            return true;
        }
        return m_space;
    case QEvent::MouseButtonRelease:
        if (e->button() != Qt::LeftButton || !m_panning) return m_space;
        m_panning = false;
        m_area->viewport()->setCursor(Qt::OpenHandCursor);
        return true;
    default:
        return false;
    }
}

bool HoldKeys::eventFilter(QObject *obj, QEvent *e)
{
    if (obj != m_area) return false;

    switch (e->type()) {
    case QEvent::KeyPress: {
        auto *ke = static_cast<QKeyEvent *>(e);
        if (ke->key() == Qt::Key_Shift) setShift(true);
        if (ke->key() != Qt::Key_Space) return false;
        if (!m_space && !ke->isAutoRepeat()
                && QGuiApplication::mouseButtons() == Qt::NoButton) {
            m_space  = true;
            m_cursor = m_area->viewport()->cursor();
            m_area->viewport()->setCursor(Qt::OpenHandCursor);
        }
        return m_space;
    }
    case QEvent::KeyRelease: {
        auto *ke = static_cast<QKeyEvent *>(e);
        if (ke->key() == Qt::Key_Shift) setShift(false);
        if (ke->key() != Qt::Key_Space || !m_space) return false;
        if (!ke->isAutoRepeat()) endSpace();
        return true;
    }
    case QEvent::FocusOut:
        setShift(false);
        if (m_space) endSpace();
        return false;
    default:
        return false;
    }
}

void HoldKeys::setShift(bool held)
{
    if (m_shift == held) return;
    m_shift = held;
    Q_EMIT shiftChanged(held);
}

void HoldKeys::endSpace()
{
    m_space = false;
    if (m_panTool) return;
    m_panning = false;
    m_area->viewport()->setCursor(m_cursor);
}
