#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QRectF>

#include <utility>

/// Finds rows of letters on a page that no word read so far covers or that stand far apart, as figures in a form.
namespace OcrMissed {

struct Row {
    QList<QRect> letters;
    QRect bounds;
    bool over { false };

    QList<std::pair<int, int>> words() const;
};

QList<Row> rows(const QImage &gray, const QList<QRectF> &read, double pxPerPt, QList<int> *upright = nullptr);

QImage closedUp(const QImage &gray, const Row &row);

}
