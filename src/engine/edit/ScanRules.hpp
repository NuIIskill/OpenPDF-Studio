#pragma once

#include <QImage>
#include <QList>
#include <QRect>

/// Finds the rules of a form in a scan's pixels: lines across and upright, drawn through, dotted or dashed.
namespace ScanRules {

QList<QRect> find(const QImage &picture, double pxPerPt);

}
