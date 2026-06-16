#include "TextBoxFrame.hpp"

#include <QPainter>
#include <QPen>

TextBoxFrame::TextBoxFrame(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);
    hide();
}

void TextBoxFrame::presentAround(const QRectF &bounds)
{
    const QRect outer = bounds.toAlignedRect().adjusted(-kPad, -kPad, kPad, kPad);
    setGeometry(outer);
    raise();
    show();
    update();
}

void TextBoxFrame::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Dashed blue border inset from widget edges by half handle size
    const int h  = kHandle / 2;
    const QRect border = rect().adjusted(h, h, -h, -h);
    QPen borderPen(QColor(0x3B, 0x82, 0xF6), 2, Qt::CustomDashLine);
    borderPen.setDashPattern({4.0, 3.0});
    p.setPen(borderPen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(border);

    // 8 solid blue handle squares at corners + midpoints
    const int hw = kHandle;
    const int W  = width()  - hw;
    const int H  = height() - hw;
    const QPoint pts[] = {
        {0,    0},    {W/2, 0},   {W, 0},
        {0,    H/2},              {W, H/2},
        {0,    H},    {W/2, H},   {W, H},
    };
    p.setPen(QPen(QColor(0x3B, 0x82, 0xF6), 1));
    p.setBrush(QColor(0x3B, 0x82, 0xF6));
    for (const QPoint &pt : pts)
        p.drawRect(QRect(pt, QSize(hw, hw)));
}
