#include "ui/widgets/CheckBoxStyle.hpp"
#include "ui/theme/Theme.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>

int CheckBoxStyle::pixelMetric(PixelMetric metric, const QStyleOption *option,
                               const QWidget *widget) const
{
    if (metric == PM_IndicatorWidth || metric == PM_IndicatorHeight
            || metric == PM_ExclusiveIndicatorWidth || metric == PM_ExclusiveIndicatorHeight)
        return 18;
    return QProxyStyle::pixelMetric(metric, option, widget);
}

void CheckBoxStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                                  QPainter *p, const QWidget *widget) const
{
    if (element != PE_IndicatorCheckBox && element != PE_IndicatorRadioButton) {
        QProxyStyle::drawPrimitive(element, option, p, widget);
        return;
    }
    const int side = qMin(option->rect.width(), option->rect.height());
    const QRectF box = QRectF(option->rect.x(), option->rect.center().y() - side / 2.0 + 0.5,
                              side, side).adjusted(0.75, 0.75, -0.75, -0.75);
    const bool on = option->state & State_On;
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    if (!(option->state & State_Enabled)) p->setOpacity(0.45);
    if (element == PE_IndicatorRadioButton) {
        p->setBrush(option->palette.color(QPalette::Base));
        p->setPen(QPen(on ? Theme::Primary : Theme::IconMuted, on ? 2.0 : 1.5));
        p->drawEllipse(box);
        if (on) {
            p->setPen(Qt::NoPen);
            p->setBrush(Theme::Primary);
            p->drawEllipse(box.center(), box.width() * 0.25, box.height() * 0.25);
        }
    } else if (on) {
        p->setPen(Qt::NoPen);
        p->setBrush(Theme::Primary);
        p->drawRoundedRect(box, 4, 4);
        QPainterPath tick;
        tick.moveTo(box.left() + box.width() * 0.26, box.top() + box.height() * 0.52);
        tick.lineTo(box.left() + box.width() * 0.44, box.top() + box.height() * 0.70);
        tick.lineTo(box.left() + box.width() * 0.75, box.top() + box.height() * 0.31);
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawPath(tick);
    } else {
        p->setBrush(option->palette.color(QPalette::Base));
        p->setPen(QPen(Theme::IconMuted, 1.5));
        p->drawRoundedRect(box, 4, 4);
    }
    p->restore();
}
