#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "fpdfview.h"

#include <QImage>
#include <QRect>
#include <QSize>

/// Renders a part of a PDFium page into a bitmap in which it comes out as the whole page shows it.
namespace PdfiumPart {

struct Frame {
    QRect  area;
    QPoint part;

    int startX() const { return -area.x(); }
    int startY() const { return -area.y(); }
};

Frame frameFor(FPDF_PAGE page, const QRect &part, const QSize &pagePx);

QImage cut(FPDF_BITMAP bitmap, const Frame &frame, const QSize &part);

}

#endif
