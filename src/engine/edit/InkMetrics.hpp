#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QList>
#include <QRect>
#include <QRectF>
#include <QString>

namespace InkMetrics {

/// Stores measured ink bounds for one PDF text line.
struct MeasuredInk { double top { 0.0 }; double height { 0.0 }; double left { 0.0 }; };

/// Stores the ink of an area: its bounds and the share of them it fills.
struct Blob { QRectF rect; double fill { 0.0 }; };

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

QRectF inkAroundPt(const QImage &img, const QRectF &linePt, qreal scale);

QRectF inkOnBaselinePt(const QImage &img, const QRectF &linePt, double baselinePt, qreal scale);

bool ruledBetweenPt(const QImage &img, const QRectF &abovePt, const QRectF &belowPt, qreal scale);

double strokeWidthPt(const QImage &img, const QRectF &inkPt, qreal scale);

Blob blobPt(const QImage &img, const QRectF &areaPt, qreal scale);

double slopePx(const QImage &img, const QRect &px, int cut);

double slopePt(const QImage &img, const QRectF &areaPt, qreal scale);

double gapMiddlePt(const QImage &img, const QRectF &areaPt, qreal scale);

QList<std::pair<double, double>> gapsPt(const QImage &img, const QRectF &areaPt, qreal scale, double slope = 0.0);

}
