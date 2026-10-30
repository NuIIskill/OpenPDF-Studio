#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "fpdfview.h"

#include <QByteArray>
#include <QList>
#include <QRectF>
#include <QSize>
#include <QStringList>
#include <QTransform>

/// Takes everything under the redacted areas of a page out of its objects and covers the areas.
namespace PdfiumRedaction {

struct Raster {
    QByteArray rgb;
    QSize      size;
    QTransform placement;
    QList<QRectF> bars;
};

bool apply(FPDF_DOCUMENT doc, FPDF_PAGE page, int pageIndex, const QList<QRectF> &areas,
           bool asImage, bool &inPlace, Raster *raster);

QStringList leaks(FPDF_DOCUMENT doc, FPDF_PAGE page, const QList<QRectF> &areas);

}

#endif
