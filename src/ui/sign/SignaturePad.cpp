#include "ui/sign/SignaturePad.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

SignaturePad::SignaturePad(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StaticContents);
    setCursor(Qt::CrossCursor);
}

void SignaturePad::setPenColor(const QColor &color)
{
    m_color = color;
    update();
}

void SignaturePad::setPenWidth(qreal width)
{
    m_width = width;
    update();
}

void SignaturePad::clear()
{
    if (m_strokes.isEmpty()) return;
    m_strokes.clear();
    update();
    Q_EMIT changed();
}

QImage SignaturePad::toImage(qreal scale) const
{
    if (m_strokes.isEmpty()) return {};

    qreal left = 1e9, top = 1e9, right = -1e9, bottom = -1e9;
    for (const QPolygonF &stroke : m_strokes)
        for (const QPointF &pt : stroke) {
            left  = qMin(left, pt.x());  top    = qMin(top, pt.y());
            right = qMax(right, pt.x()); bottom = qMax(bottom, pt.y());
        }
    const QRectF bounds = QRectF(QPointF(left, top), QPointF(right, bottom))
                              .adjusted(-m_width, -m_width, m_width, m_width);

    QImage img((bounds.size() * scale).toSize().expandedTo(QSize(1, 1)),
               QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.scale(scale, scale);
    p.translate(-bounds.topLeft());
    paintStrokes(p);
    return img;
}

void SignaturePad::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    paintStrokes(p);
}

void SignaturePad::paintStrokes(QPainter &p) const
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    for (const QPolygonF &stroke : m_strokes) {
        if (stroke.size() == 1) {
            p.drawPoint(stroke.first());
            continue;
        }
        // Midpoint quadratics smooth the jagged mouse samples into one curve.
        QPainterPath path(stroke.first());
        for (int i = 1; i < stroke.size() - 1; ++i)
            path.quadTo(stroke[i], (stroke[i] + stroke[i + 1]) / 2.0);
        path.lineTo(stroke.last());
        p.drawPath(path);
    }
}

void SignaturePad::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    m_drawing = true;
    m_strokes.append(QPolygonF{ event->position() });
    update();
    Q_EMIT changed();
}

void SignaturePad::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_drawing) return;
    m_strokes.last().append(event->position());
    update();
}

void SignaturePad::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_drawing = false;
}
