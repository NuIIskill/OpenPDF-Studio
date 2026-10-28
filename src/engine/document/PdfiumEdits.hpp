#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/edit/EditSession.hpp"
#include "engine/edit/TextLayout.hpp"

#include "fpdfview.h"

#include <QHash>

/// Applies session edits to a PDFium page object list.
namespace PdfiumEdits {

void applyToPage(FPDF_DOCUMENT doc, FPDF_PAGE page, int pageIndex,
                 const EditSession &session, const QHash<int, FPDF_PAGE> *sourcePages = nullptr);

void applyNoteEdits(FPDF_PAGE page, int pageIndex, const EditSession &session);

TextLayout::Metrics metrics(FPDF_DOCUMENT doc, FPDF_PAGE page, FPDF_TEXTPAGE textPage,
                            const EditSession::Edit &edit);

void textAlone(FPDF_DOCUMENT doc, FPDF_PAGE page, const QList<EditSession::Edit> &edits);

QList<TextLayout::OriginalLine> laidLines(FPDF_DOCUMENT doc, FPDF_PAGE page, FPDF_TEXTPAGE textPage,
                                          const EditSession::Edit &edit,
                                          QList<QList<TextLayout::Source>> *sources,
                                          QList<QList<TextLayout::Mark>> *marks = nullptr);

}

#endif
