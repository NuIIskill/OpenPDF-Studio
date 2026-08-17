#pragma once

#include "engine/edit/TextBlock.hpp"

#include <QList>
#include <QPointF>
#include <QRectF>

#ifdef HAVE_POPPLER
#include <poppler/qt6/poppler-qt6.h>

// Text lookup on the Poppler backend — the counterpart to PdfTextExtractor,
// which only exists when Qt PDF is available. Both answer the same three
// questions the inline editor asks; only the source of the word boxes differs.
//
// Coordinates are PDF points with a top-left origin (Y down), which is what
// Poppler-Qt6 reports for text boxes.
//
// `exclude` lists regions whose text must be treated as GONE (session-erased
// areas): a word whose centre falls inside one is invisible to every lookup.
namespace PopplerText {

/// Text line at `pdfPt`, or an invalid TextBlock if no word is close enough.
TextBlock textAt(Poppler::Document *doc, int page, const QPointF &pdfPt,
                 const QList<QRectF> &exclude = {});

/// Full text of the block covering `rect`, lines joined with '\n', plus the
/// glyph-accurate united bounds of the words that were collected.
TextBlock blockInRect(Poppler::Document *doc, int page, const QRectF &rect,
                      const QList<QRectF> &exclude = {});

/// Tight word boxes of the block covering `area` — used to erase ONLY the
/// glyphs when hiding original text, never the whole area.
QList<QRectF> glyphRects(Poppler::Document *doc, int page, const QRectF &area,
                         const QList<QRectF> &exclude = {});

} // namespace PopplerText

#endif // HAVE_POPPLER
