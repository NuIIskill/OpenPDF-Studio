#pragma once

#ifdef HAVE_QPDF

#include "engine/export/PdfA.hpp"
#include "engine/export/PdfAContent.hpp"

/// Embeds an installed font with the same character widths for every font a PDF shows without its program.
namespace PdfAFonts {

void embed(QPDF &pdf, const PdfAContent::Use &use, PdfA::Report &report);

}

#endif
