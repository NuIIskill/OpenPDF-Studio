// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/engine/PosterFrame.hpp"

#include "rich-media/engine/VideoStill.hpp"

#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>

bool PosterFrame::available()
{
    return true;
}

QImage PosterFrame::grab(const QString &videoPath, int maxWidth)
{
    return VideoStill::grab(videoPath, maxWidth);
}

QImage PosterFrame::placeholder(const QSize &size, const QColor &tint)
{
    QSize target = size;
    if (target.width() < 32 || target.height() < 24) target = QSize(640, 360);

    QImage image(target, QImage::Format_ARGB32_Premultiplied);
    image.fill(tint.isValid() ? tint : QColor(0x1E, 0x22, 0x2B));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal radius = qMax(18.0, qMin(target.width(), target.height()) / 6.0);
    const QPointF centre(target.width() / 2.0, target.height() / 2.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 235));
    painter.drawEllipse(centre, radius, radius);

    const qreal side = radius * 0.78;
    QPainterPath triangle;
    triangle.moveTo(centre.x() - side * 0.42, centre.y() - side * 0.62);
    triangle.lineTo(centre.x() - side * 0.42, centre.y() + side * 0.62);
    triangle.lineTo(centre.x() + side * 0.66, centre.y());
    triangle.closeSubpath();
    painter.setBrush(QColor(0x1E, 0x22, 0x2B));
    painter.drawPath(triangle);

    painter.end();
    return image;
}
