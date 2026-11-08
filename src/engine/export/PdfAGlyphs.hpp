#pragma once

#ifdef HAVE_QPDF

#include "engine/export/PdfA.hpp"
#include "engine/export/PdfAContent.hpp"

/// Checks that embedded fonts hold every glyph the document shows, with the widths their dictionaries give.
namespace PdfAGlyphs {

void check(QPDF &pdf, const PdfAContent::Use &use, PdfA::Report &report);

}

#endif
