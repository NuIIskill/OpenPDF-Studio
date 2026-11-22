#pragma once

#include <QImage>

/// Takes handwriting out of a scan's pixels where it can be told from the print: a pen's strokes in another colour, and single long curves.
namespace ScanPen {

QImage without(const QImage &picture, double pxPerPt);

QImage withoutCurves(const QImage &gray, double pxPerPt);

}
