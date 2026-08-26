#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "fpdfview.h"

#include <QString>

/// Makes embedded PDF fonts available to Qt.
namespace PdfiumFonts {

QString registerWithQt(FPDF_FONT font);

} // namespace PdfiumFonts

#endif
