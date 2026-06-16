#ifdef HAVE_QT_PDF

#include "PdfTextExtractor.hpp"
#include <QPdfSelection>

PdfTextExtractor::PdfTextExtractor(QPdfDocument *doc)
    : m_doc(doc)
{
}

// ── Public ────────────────────────────────────────────────────────────────────

TextBlock PdfTextExtractor::textAt(int page, const QPointF &pdfPt,
                                   const QSizeF &pageSizePts) const
{
    // Strategy: getAllText gives us the actual text-run bounding polygons.
    // We find which polygon the click fell in, then use getSelection with
    // those exact bounds to retrieve the text.  This is far more reliable
    // than guessing a wide horizontal band upfront.

    const QPdfSelection all = m_doc->getAllText(page);
    if (!all.isValid()) return {};

    const QList<QPolygonF> &polys = all.bounds();

    // Pass 1: strict containment (exact hit inside a text run)
    for (const QPolygonF &poly : polys) {
        const QRectF r = poly.boundingRect();
        if (r.contains(pdfPt)) {
            TextBlock blk = fetchBlock(page, r, pageSizePts);
            if (blk.isValid()) return blk;
        }
    }

    // Pass 2: expanded tolerance (+/- 4 pts vertically) for clicks
    //         between tightly-spaced lines or in thin descenders.
    for (const QPolygonF &poly : polys) {
        const QRectF r = poly.boundingRect().adjusted(0, -4, 0, 4);
        if (r.contains(pdfPt)) {
            TextBlock blk = fetchBlock(page, poly.boundingRect(), pageSizePts);
            if (blk.isValid()) return blk;
        }
    }

    // Pass 3: closest block by Y distance (if click is in a gap between lines)
    const QPolygonF *best = nullptr;
    qreal bestDist = 12.0; // max 12 pts gap to snap to
    for (const QPolygonF &poly : polys) {
        const QRectF r = poly.boundingRect();
        const qreal dy = (pdfPt.y() < r.top())   ? r.top()    - pdfPt.y()
                       : (pdfPt.y() > r.bottom()) ? pdfPt.y() - r.bottom()
                       : 0;
        if (dy < bestDist) { bestDist = dy; best = &poly; }
    }
    if (best) {
        TextBlock blk = fetchBlock(page, best->boundingRect(), pageSizePts);
        if (blk.isValid()) return blk;
    }

    return {};
}

QList<TextBlock> PdfTextExtractor::allBlocks(int page) const
{
    QList<TextBlock> result;
    const QSizeF pageSize = m_doc->pagePointSize(page);
    const QPdfSelection all = m_doc->getAllText(page);
    if (!all.isValid()) return result;
    for (const QPolygonF &poly : all.bounds()) {
        TextBlock blk = fetchBlock(page, poly.boundingRect(), pageSize);
        if (blk.isValid())
            result.append(blk);
    }
    return result;
}

// ── Private ───────────────────────────────────────────────────────────────────

TextBlock PdfTextExtractor::fetchBlock(int page, const QRectF &r,
                                       const QSizeF & /*pageSizePts*/) const
{
    // Use the polygon's own top-left / bottom-right as the selection range.
    // getSelection snaps to the nearest characters at those two positions,
    // so passing the exact text-run corners reliably returns that run's text.
    const QPdfSelection sel = m_doc->getSelection(page, r.topLeft(), r.bottomRight());
    if (!sel.isValid() || sel.text().trimmed().isEmpty()) return {};

    // Prefer the bounds Qt reports for the selection (may be slightly wider
    // than our polygon bound if glyphs extend past their logical bounds).
    QRectF bounds = r;
    if (!sel.bounds().isEmpty())
        bounds = sel.bounds().first().boundingRect();

    return { page, bounds, sel.text() };
}

#endif // HAVE_QT_PDF
