#pragma once

#include <QImage>
#include <QList>
#include <QPainterPath>

/// Tells which of some glyph shapes a piece of a scan's ink shows.
namespace ScanLike {

QList<double> scores(const QImage &ink, const QList<QPainterPath> &shapes);

int shown(const QImage &ink, const QList<QPainterPath> &shapes);

double alike(const QPainterPath &a, const QPainterPath &b);

}
