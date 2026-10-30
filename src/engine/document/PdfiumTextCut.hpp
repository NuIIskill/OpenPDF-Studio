#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/edit/TextLayout.hpp"

#include "fpdfview.h"

#include <QHash>
#include <QList>
#include <QRectF>
#include <QSet>

#include <memory>

class PdfiumPageSpace;

namespace PdfiumText { class Typeset; }

/// The text objects that areas take characters out of, and what of them lies outside.
namespace PdfiumTextCut {

enum class Inside { Centre, Touching };

struct Result {
    QList<FPDF_PAGEOBJECT>          cut;
    QList<FPDF_PAGEOBJECT>          whole;
    QList<FPDF_PAGEOBJECT>          turned;
    QSet<FPDF_PAGEOBJECT>           seen;
    QList<TextLayout::OriginalLine> rest;
};

Result cut(FPDF_TEXTPAGE textPage, const QList<QRectF> &areas, const PdfiumPageSpace &space,
           Inside inside);

std::shared_ptr<PdfiumText::Typeset> setAgain(FPDF_DOCUMENT doc, FPDF_PAGE page,
                                              FPDF_TEXTPAGE textPage, int pageIndex,
                                              const TextLayout::OriginalLine &line,
                                              const QHash<int, FPDF_PAGE> *sourcePages);

}

#endif
