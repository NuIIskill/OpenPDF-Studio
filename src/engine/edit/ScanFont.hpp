#pragma once

#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

/// Finds the installed font, weight and size a scanned line was printed in by comparing its pixels.
namespace ScanFont {

struct Line { QRectF inkPt; QString text; QRectF textPt; };

struct Fit {
    double sizePt { 0.0 };
    double score { 0.0 };
    bool bold { false };
    QPointF originPt;
    double spacingPt { 0.0 };
};

struct Match {
    QString family;
    bool bold { false };
    double sizePt { 0.0 };
    QList<Fit> lines;
    bool isValid() const { return !family.isEmpty() && sizePt > 0.0; }
};

Match match(const QImage &img, qreal scale, const QList<Line> &lines, const QList<Line> &context = {},
            const QString &family = {});

}
