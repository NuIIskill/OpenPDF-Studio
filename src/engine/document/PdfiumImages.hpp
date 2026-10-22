#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/edit/EditSession.hpp"

#include "fpdfview.h"

#include <QImage>
#include <QList>
#include <QRectF>

#include <vector>

/// Finds, extracts and removes the image objects of a PDFium page.
namespace PdfiumImages {

struct Found {
    QList<int>      key;
    QRectF          visible;
    FPDF_PAGEOBJECT object { nullptr };
    QRectF          bounds;
};

QList<Found> find(FPDF_PAGE page);

QImage extract(FPDF_DOCUMENT doc, FPDF_PAGE page, const Found &found);

void apply(FPDF_DOCUMENT doc, FPDF_PAGE page, const QList<EditSession::ImageChange> &changes,
           std::vector<FPDF_PAGEOBJECT> &removed);

}

#endif
