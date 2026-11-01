#include "ui/view/ScrollBarReach.hpp"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionSlider>

namespace {

constexpr int kReach = 14;

}

ScrollBarReach::ScrollBarReach(QScrollBar *bar, QAbstractScrollArea *area)
    : QWidget(area)
    , m_bar(bar)
    , m_area(area)
{
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    hide();
    connect(bar, &QScrollBar::valueChanged, this, &ScrollBarReach::place);
    connect(bar, &QScrollBar::rangeChanged, this, &ScrollBarReach::place);
    bar->installEventFilter(this);
    area->installEventFilter(this);
}

bool ScrollBarReach::eventFilter(QObject *, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::StyleChange:
        place();
        break;
    default:
        break;
    }
    return false;
}

void ScrollBarReach::place()
{
    if (!m_bar || !m_bar->isVisible() || m_bar->maximum() <= m_bar->minimum()) {
        hide();
        return;
    }
    QStyleOptionSlider opt;
    opt.initFrom(m_bar);
    opt.orientation    = m_bar->orientation();
    opt.minimum        = m_bar->minimum();
    opt.maximum        = m_bar->maximum();
    opt.pageStep       = m_bar->pageStep();
    opt.singleStep     = m_bar->singleStep();
    opt.sliderPosition = m_bar->sliderPosition();
    opt.sliderValue    = m_bar->value();
    opt.upsideDown     = m_bar->invertedAppearance();
    opt.subControls    = QStyle::SC_All;
    const QRect slider = m_bar->style()->subControlRect(
        QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarSlider, m_bar);
    const QPoint at = m_bar->mapTo(m_area, slider.topLeft());
    if (m_bar->orientation() == Qt::Vertical)
        setGeometry(at.x() - kReach, at.y(), kReach, slider.height());
    else
        setGeometry(at.x(), at.y() - kReach, slider.width(), kReach);
    show();
    raise();
}

QPointF ScrollBarReach::onBar(const QPointF &global) const
{
    QPointF p = m_bar->mapFromGlobal(global);
    if (m_bar->orientation() == Qt::Vertical)
        p.setX(m_bar->width() / 2.0);
    else
        p.setY(m_bar->height() / 2.0);
    return p;
}

void ScrollBarReach::forward(QMouseEvent *e)
{
    if (!m_bar) return;
    const QPointF p = onBar(e->globalPosition());
    QMouseEvent moved(e->type(), p, m_bar->mapToGlobal(p), e->button(), e->buttons(),
                      e->modifiers());
    QApplication::sendEvent(m_bar, &moved);
}

void ScrollBarReach::hover(QEvent::Type type, const QPointF &global)
{
    if (!m_bar) return;
    const QPointF p = onBar(global);
    QHoverEvent event(type, p, m_bar->mapToGlobal(p), p);
    QApplication::sendEvent(m_bar, &event);
}

void ScrollBarReach::mousePressEvent(QMouseEvent *e)
{
    forward(e);
}

void ScrollBarReach::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() != Qt::NoButton)
        forward(e);
    else
        hover(QEvent::HoverMove, e->globalPosition());
}

void ScrollBarReach::mouseReleaseEvent(QMouseEvent *e)
{
    forward(e);
}

void ScrollBarReach::leaveEvent(QEvent *)
{
    hover(QEvent::HoverLeave, QCursor::pos());
}
