#include "ui/forms/FormBox.hpp"

#include "ui/forms/FormLayer.hpp"
#include "ui/theme/Theme.hpp"

#include <QMouseEvent>
#include <QPainter>

FormBox::FormBox(FormLayer *layer, int index, QWidget *parent)
    : QWidget(parent), m_layer(layer), m_index(index)
{
    setAttribute(Qt::WA_Hover);
    hide();
}

void FormBox::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    if (m_layer->m_editing == m_index && m_layer->m_editor) {
        painter.fillRect(rect(), Qt::white);
        painter.setPen(QPen(Theme::Primary, 1));
        painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
        return;
    }
    QColor fill = Theme::Primary;
    fill.setAlpha(m_hover ? 46 : 26);
    painter.fillRect(rect(), fill);
    if (m_layer->missing(m_index)) {
        painter.setPen(QPen(QColor(0xDC, 0x26, 0x26), 1.5));
        painter.drawRect(QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75));
    } else if (m_hover) {
        painter.setPen(QPen(Theme::Primary, 1));
        painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    }
}

bool FormBox::event(QEvent *event)
{
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave) {
        m_hover = event->type() == QEvent::HoverEnter;
        update();
    }
    return QWidget::event(event);
}

void FormBox::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return QWidget::mousePressEvent(event);
    event->accept();
    m_layer->activate(m_index, event->position().toPoint());
}
