#pragma once

#include "TextBlock.hpp"
#include <QPointF>
#include <QSizeF>
#include <QList>

#ifdef HAVE_QT_PDF
#include <QPdfDocument>

class PdfTextExtractor
{
public:
    explicit PdfTextExtractor(QPdfDocument *doc);

    // Find the text block (line) the user clicked on.
    // pdfPt is in PDF-point view coordinates (top-left origin, Y down).
    // pageSizePts is the page's full size in PDF points.
    // exclude: regions whose text must be treated as GONE (session-erased
    // areas) — glyphs inside them are invisible to the lookup.
    // Returns an invalid TextBlock if no text found at that position.
    TextBlock textAt(int page, const QPointF &pdfPt, const QSizeF &pageSizePts,
                     const QList<QRectF> &exclude = {}) const;

    // All text blocks on a page (for diagnostics / future use).
    QList<TextBlock> allBlocks(int page) const;

    // Text inside a rectangle (PDF-point coords, Y=0 at top), lines separated
    // by '\n'. Used to fetch the full text of a detected paragraph block.
    QString textInRect(int page, const QRectF &rect) const;

    // Like textInRect, but also returns the glyph-accurate united bounds of
    // the selection — tighter than the region model's estimated width.
    TextBlock blockInRect(int page, const QRectF &rect,
                          const QList<QRectF> &exclude = {}) const;

    // Tight per-line glyph rects of the text inside `area`. Used to erase
    // ONLY the glyphs when hiding original text — never the whole area.
    QList<QRectF> glyphRects(int page, const QRectF &area,
                             const QList<QRectF> &exclude = {}) const;

private:
    TextBlock fetchBlock(int page, const QRectF &r, const QSizeF &pageSizePts) const;

    QPdfDocument *m_doc;
};

#endif // HAVE_QT_PDF
