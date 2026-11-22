#pragma once

#include "engine/edit/InkMetrics.hpp"
#include "engine/edit/TextLayout.hpp"

#include <QList>
#include <QRectF>

#include <functional>
#include <utility>

/// Cleans the lines OCR read from a scan: columns read as one, specks read as text, misread marks.
namespace ScanCleanup {

// The size a line's marks are measured at and where it stands at a point across.
using LineMeasure = std::function<std::pair<double, double>(int line, double x)>;

bool keepColumn(QList<TextLayout::OriginalLine> &lines, const QList<TextLayout::OriginalLine> &around,
                double clickX);

bool keepSide(QList<TextLayout::OriginalLine> &lines, double clickX);

bool joinPieces(QList<TextLayout::OriginalLine> &lines, const QRectF &within);

bool dropSpecks(QList<TextLayout::OriginalLine> &lines);

bool dropSpeckMarks(QList<TextLayout::OriginalLine> &lines);

bool partFigures(QList<TextLayout::OriginalLine> &lines, double spacePt,
                 const std::function<QList<std::pair<double, double>>(int line)> &gapsOf);

bool readMarks(QList<TextLayout::OriginalLine> &lines, const LineMeasure &measure,
               const std::function<InkMetrics::Blob(const QRectF &)> &blobAt);

}
