#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/edit/EditSession.hpp"
#include "engine/edit/TextLayout.hpp"

#include "fpdfview.h"

/// Applies session edits to a PDFium page object list.
namespace PdfiumEdits {

void applyToPage(FPDF_DOCUMENT doc, FPDF_PAGE page, int pageIndex,
                 const EditSession &session);

void applyNoteEdits(FPDF_PAGE page, int pageIndex, const EditSession &session);

TextLayout::Metrics metrics(FPDF_DOCUMENT doc, FPDF_PAGE page,
                            const EditSession::Edit &edit);

}

#endif
