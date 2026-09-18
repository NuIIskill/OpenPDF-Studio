#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QString>

namespace InkMetrics {

/// Stores measured ink bounds for one PDF text line.
struct MeasuredInk { double top { 0.0 }; double height { 0.0 }; double left { 0.0 }; };

/// Stores normalized ink metrics for a font sample.
struct FontInk {
    double heightPerPt { 0.0 };
    double risePerPt   { 0.0 };
    double bearingPerPt{ 0.0 };
};

QColor sampleTextColor(const QImage &img, const QRect &region);

QColor sampleBackgroundColor(const QImage &img, const QRect &region);

MeasuredInk measuredInkPt(const QImage &img, const QRectF &boundsPt, qreal scale);

FontInk fontInkPerPt(const QString &text, QFont f);

}
