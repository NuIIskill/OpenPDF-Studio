#pragma once

#ifdef HAVE_QPDF

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

/// Draws the appearance PDF/A needs for an annotation that leaves its drawing to the reader.
namespace PdfAAppearances {

bool draw(QPDF &pdf, QPDFObjectHandle annot);

}

#endif
