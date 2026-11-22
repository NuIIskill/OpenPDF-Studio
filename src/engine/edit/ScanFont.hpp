#pragma once

#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

/// Finds the installed font, weight and size a scanned line was printed in by comparing its pixels.
namespace ScanFont {

struct Line {
    QRectF inkPt; QString text; QRectF textPt; QList<double> wordStartsPt; double basePt { -1.0 }; double tilt { 0.0 };
    QList<QRectF> foreignPt;
};

struct Fit {
    double sizePt { 0.0 };
    double score { 0.0 };
    bool bold { false };
    QPointF originPt;
    double spacingPt { 0.0 };
    double boldMargin { 0.0 };
    double inkGray { -1.0 };
    QStringList words;
    QList<double> wordMiddlesPt;
    QList<double> wordBaselinesPt;
    double slope { 0.0 };
    double slopeReachPt { 0.0 };
    bool italic { false };
    QList<bool> wordBold;
    QList<bool> wordItalic;
    bool byHeight { false };
};

struct Match {
    QString family;
    bool bold { false };
    bool kerned { false };
    bool italic { false };
    double sizePt { 0.0 };
    double inkGray { -1.0 };
    QList<Fit> lines;
    bool isValid() const { return !family.isEmpty() && sizePt > 0.0; }
};

Match match(const QImage &img, qreal scale, const QList<Line> &lines, const QList<Line> &context = {},
            const QString &family = {}, int blackTone = -1);

}
