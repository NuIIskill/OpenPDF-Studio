#include "PagePlaceholder.hpp"

#include <QPainter>
#include <QPainterPath>

PagePlaceholder::PagePlaceholder(int pageNumber, QWidget *parent)
    : QWidget(parent)
    , m_pageNumber(pageNumber)
{
    setObjectName(QStringLiteral("PagePlaceholder"));
    setFixedSize(595, 842);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void PagePlaceholder::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r(0, 0, width(), height());

    // Drop shadow (layered)
    for (int i = 6; i >= 1; --i) {
        const int alpha = static_cast<int>(6.0 / i * 8);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, alpha));
        p.drawRoundedRect(r.adjusted(i * 0.5, i, -i * 0.5, i * 0.5), 4, 4);
    }

    // White page surface
    p.setBrush(Qt::white);
    p.setPen(QPen(QColor(0, 0, 0, 18), 1.0));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
}
