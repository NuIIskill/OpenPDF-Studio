#include "ui/edit/EditCallout.hpp"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {

constexpr qreal kCard = 7.0;
constexpr qreal kRadius = 6.0;
const QColor kBlue(0x3B, 0x82, 0xF6);

QPointF nearestOn(const QRectF &r, const QPointF &p)
{
    return QPointF(qBound(r.left(), p.x(), r.right()), qBound(r.top(), p.y(), r.bottom()));
}

}

EditCallout::EditCallout(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
    hide();
}

QRectF EditCallout::chipRect() const
{
    return QRectF(m_box.left() - kCard, m_box.top() - kCard - 26.0, 64.0, 22.0);
}

void EditCallout::point(const QRectF &box, const QPolygonF &target, double turn)
{
    m_box    = box;
    m_target = target;
    m_turn   = turn;
    const QRect area = box.adjusted(-kCard - 6, -kCard - 6, kCard + 6, kCard + 6)
                           .united(chipRect().adjusted(-2, -2, 2, 2))
                           .united(target.boundingRect().adjusted(-4, -4, 4, 4))
                           .toAlignedRect();
    m_box.translate(-area.topLeft());
    m_target.translate(-area.topLeft());
    setGeometry(area);
    show();
    update();
}

void EditCallout::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // The text that is edited, tinted where it stands.
    QPen outline(kBlue, 1.0, Qt::CustomDashLine);
    outline.setDashPattern({ 4.0, 3.0 });
    p.setPen(outline);
    p.setBrush(QColor(0x3B, 0x82, 0xF6, 22));
    p.drawPolygon(m_target);

    // A card around the box, leaving the box itself to the page, which draws the text.
    const QRectF card = m_box.adjusted(-kCard, -kCard, kCard, kCard);
    for (int i = 4; i >= 1; --i) {
        QPainterPath shadow;
        shadow.addRoundedRect(card.adjusted(-i, -i + 2, i, i + 2), kRadius + i, kRadius + i);
        shadow.addRect(m_box);
        p.fillPath(shadow, QColor(0, 0, 0, 10));
    }
    QPainterPath ring;
    ring.addRoundedRect(card, kRadius, kRadius);
    ring.addRect(m_box);
    p.fillPath(ring, Qt::white);
    p.setPen(QPen(QColor(0xCB, 0xD5, 0xE1), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(card, kRadius, kRadius);

    // How the text is turned: a small frame turned the same way, its reading
    // direction and the angle.
    const QRectF chip = chipRect();
    p.setPen(QPen(QColor(0xCB, 0xD5, 0xE1), 1.0));
    p.setBrush(Qt::white);
    p.drawRoundedRect(chip, chip.height() / 2.0, chip.height() / 2.0);
    p.save();
    p.translate(chip.left() + 15.0, chip.center().y());
    p.rotate(m_turn);
    p.setPen(QPen(kBlue, 1.2));
    p.setBrush(QColor(0x3B, 0x82, 0xF6, 30));
    p.drawRect(QRectF(-8.0, -4.0, 16.0, 8.0));
    p.drawLine(QPointF(-5.0, 0.0), QPointF(3.0, 0.0));
    QPainterPath tip;
    tip.moveTo(5.5, 0.0);
    tip.lineTo(2.5, -2.5);
    tip.lineTo(2.5, 2.5);
    tip.closeSubpath();
    p.fillPath(tip, kBlue);
    p.restore();
    double shown = std::fmod(m_turn, 360.0);
    if (shown > 180.0) shown -= 360.0;
    if (shown <= -180.0) shown += 360.0;
    QFont small = font();
    small.setPixelSize(11);
    p.setFont(small);
    p.setPen(QColor(0x33, 0x41, 0x55));
    p.drawText(chip.adjusted(28.0, 0.0, -6.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("%1°").arg(qRound(shown)));

    // From outside the card, nearest the text, to the side of the text nearest it.
    const QRectF target = m_target.boundingRect().adjusted(-2, -2, 2, 2);
    const QRectF from = card.adjusted(-2, -2, 2, 2);
    if (target.intersects(from)) return;
    const QPointF start = nearestOn(from, target.center());
    const QPointF end = nearestOn(target, start);
    const QPointF d = end - start;
    const double length = std::hypot(d.x(), d.y());
    if (length < 8.0) return;
    const QPointF along = d / length;
    const QPointF across(-along.y(), along.x());
    p.setPen(QPen(kBlue, 1.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(start, end - along * 6.0);
    QPainterPath head;
    head.moveTo(end);
    head.lineTo(end - along * 9.0 + across * 4.5);
    head.lineTo(end - along * 9.0 - across * 4.5);
    head.closeSubpath();
    p.fillPath(head, kBlue);
    p.setBrush(kBlue);
    p.setPen(Qt::NoPen);
    p.drawEllipse(start, 2.5, 2.5);
}
