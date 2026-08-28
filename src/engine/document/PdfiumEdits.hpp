#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "fpdfview.h"

class EditSession;

/// Applies session edits to a PDFium page object list.
namespace PdfiumEdits {

void applyToPage(FPDF_DOCUMENT doc, FPDF_PAGE page, int pageIndex,
                 const EditSession &session);

void applyNoteEdits(FPDF_PAGE page, int pageIndex, const EditSession &session);

} // namespace PdfiumEdits

#endif
