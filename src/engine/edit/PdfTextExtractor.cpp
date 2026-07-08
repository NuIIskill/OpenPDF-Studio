#ifdef HAVE_QT_PDF

#include "PdfTextExtractor.hpp"
#include <QPdfSelection>
#include <algorithm>
#include <cmath>

PdfTextExtractor::PdfTextExtractor(QPdfDocument *doc)
    : m_doc(doc)
{
}

// ── Public ────────────────────────────────────────────────────────────────────

TextBlock PdfTextExtractor::textAt(int page, const QPointF &pdfPt,
                                   const QSizeF & /*pageSizePts*/) const
{
    // ── Step 1: all text polygons for the page ────────────────────────────────
    const QPdfSelection all = m_doc->getAllText(page);
    if (!all.isValid()) return {};
    const QList<QPolygonF> &polys = all.bounds();
    if (polys.isEmpty()) return {};

    // ── Step 2: find the polygon containing (or nearest to) the click point ──
    // Three passes: exact contains → ±8/6 pt expansion → nearest within 40 pt.
    int best = -1;
    for (int i = 0; i < polys.size(); ++i)
        if (polys[i].boundingRect().contains(pdfPt)) { best = i; break; }
    if (best < 0)
        for (int i = 0; i < polys.size(); ++i)
            if (polys[i].boundingRect().adjusted(-8, -6, 8, 6).contains(pdfPt)) {
                best = i; break;
            }
    if (best < 0) {
        qreal bestD = 40.0;
        for (int i = 0; i < polys.size(); ++i) {
            const QRectF r = polys[i].boundingRect();
            const qreal dx = std::max({0.0, r.left()-pdfPt.x(), pdfPt.x()-r.right()});
            const qreal dy = std::max({0.0, r.top() -pdfPt.y(), pdfPt.y()-r.bottom()});
            const qreal d  = std::hypot(dx, dy);
            if (d < bestD) { bestD = d; best = i; }
        }
    }
    if (best < 0) return {};

    // Glyph height of the single clicked polygon.
    // NOTE: origR may span the entire table when Qt PDF represents the whole
    // table block as a single polygon. wordHeight is NOT capped here — the merge
    // loop uses origRCapped (below) to prevent cascade. The wide-row path then
    // uses cellR.height() (from getSelection clusters) for reportBounds so the
    // polygon height never propagates to block.pdfBounds for wide-spanning tables.
    const QRectF origR     = polys[best].boundingRect();
    const qreal wordHeight = origR.height();

    // ── Step 3: horizontal same-line merge ────────────────────────────────────
    // Qt PDF splits some text into per-word polygons.  Merge adjacent fragments
    // on the SAME LINE and with a small horizontal gap.
    // IMPORTANT: Y-overlap is tested against a HEIGHT-CAPPED version of origR,
    // not the raw (potentially table-spanning) polygon and not the growing polyR.
    QRectF polyR = origR;
    {
        // Use the top of origR but cap the bottom to one line-height so that a
        // tall multi-row polygon doesn't drag in every row below the click point.
        const QRectF origRCapped(origR.left(), origR.top(),
                                 origR.width(), wordHeight);
        const qreal yTol   = wordHeight * 0.25;   // tight — avoids adjacent-row leakage
        const qreal maxGap = std::max(2.0, wordHeight * 0.4);
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i < polys.size(); ++i) {
                const QRectF r = polys[i].boundingRect();
                if (r.bottom() < origRCapped.top()    - yTol) continue;
                if (r.top()    > origRCapped.bottom() + yTol) continue;
                const qreal gapR = r.left()     - polyR.right();
                const qreal gapL = polyR.left() - r.right();
                if ((gapR >= 0.0 && gapR <= maxGap) || (gapL >= 0.0 && gapL <= maxGap)) {
                    polyR = polyR.united(r);
                    changed = true;
                }
            }
        }
    }

    // ── Step 4: getSelection to retrieve text content ─────────────────────────
    // For table-header rows that span > 80 % of page width: X-restrict the
    // drag to ±55 pt around the click so only the clicked column is returned.
    // For all other content: use the same-line merged bounds directly.
    const qreal pageW    = m_doc->pagePointSize(page).width();
    const bool  wideRow  = polyR.width() > pageW * 0.80;

    const auto makeSelection = [&]() -> QPdfSelection {
        if (wideRow) {
            const QPointF tl(qBound(0.0, pdfPt.x() - 55.0, pageW), polyR.top());
            const QPointF br(qBound(0.0, pdfPt.x() + 55.0, pageW), polyR.bottom());
            QPdfSelection s = m_doc->getSelection(page, tl, br);
            if (s.isValid() && !s.text().trimmed().isEmpty()) return s;
        }
        return m_doc->getSelection(page, polyR.topLeft(), polyR.bottomRight());
    };
    const QPdfSelection sel = makeSelection();
    if (!sel.isValid() || sel.text().trimmed().isEmpty()) return {};

    // ── Step 5: pick the bounding rect for the edit frame ─────────────────────
    // For non-wide content: polyR (same-line merged) is the canonical bounds.
    // For wide rows:        pick the selection cluster nearest to the click.
    QRectF cellR;
    if (!wideRow) {
        cellR = polyR;
    } else {
        const QList<QPolygonF> &clusters = sel.bounds();
        if (clusters.size() <= 1) {
            cellR = clusters.isEmpty() ? polyR : clusters.first().boundingRect();
        } else {
            for (const QPolygonF &cl : clusters) {
                const QRectF br = cl.boundingRect();
                if (pdfPt.x() >= br.left() - 4.0 && pdfPt.x() <= br.right() + 4.0 &&
                    pdfPt.y() >= br.top()  - 6.0  && pdfPt.y() <= br.bottom() + 6.0) {
                    cellR = br; break;
                }
            }
            if (cellR.isNull()) {
                qreal nearestD = 1e9;
                for (const QPolygonF &cl : clusters) {
                    const QRectF br = cl.boundingRect();
                    const qreal cx  = (br.left() + br.right())  / 2.0;
                    const qreal cy  = (br.top()  + br.bottom()) / 2.0;
                    const qreal d   = std::hypot(pdfPt.x() - cx, pdfPt.y() - cy);
                    if (d < nearestD) { nearestD = d; cellR = br; }
                }
            }
        }
        if (cellR.isNull()) cellR = polyR;
    }

    const QString text = sel.text().trimmed();
    if (text.isEmpty()) return {};

    // For wide rows the selection cluster (cellR) is tighter than origR because
    // getSelection was X-restricted to ±55 pt. Use cellR.height() directly — it
    // reflects the actual rendered glyph height and gives an accurate font-size
    // estimate in the fallback path. For normal (non-wide) rows wordHeight is
    // roughly equal to cellR.height(), so the distinction doesn't matter much.
    QRectF reportBounds = cellR;
    if (!wideRow && wordHeight > 1.0)
        reportBounds.setHeight(wordHeight);

    qWarning() << "[EXTRACT] pdfPt=" << pdfPt << "polyR=" << polyR << "cellR=" << cellR
               << "wide=" << wideRow << "wordH=" << wordHeight
               << "clusters=" << sel.bounds().size() << "text=" << text.left(40);

    return { page, reportBounds, text };
}

QString PdfTextExtractor::textInRect(int page, const QRectF &rect) const
{
    const QPdfSelection sel = m_doc->getSelection(page, rect.topLeft(),
                                                  rect.bottomRight());
    if (!sel.isValid()) return {};
    return sel.text().trimmed();
}

QList<TextBlock> PdfTextExtractor::allBlocks(int page) const
{
    QList<TextBlock> result;
    const QPdfSelection all = m_doc->getAllText(page);
    if (!all.isValid()) return result;
    for (const QPolygonF &poly : all.bounds()) {
        const QRectF r = poly.boundingRect();
        const QPdfSelection sel = m_doc->getSelection(page, r.topLeft(), r.bottomRight());
        if (sel.isValid() && !sel.text().trimmed().isEmpty())
            result.append({ page, r, sel.text() });
    }
    return result;
}

TextBlock PdfTextExtractor::fetchBlock(int page, const QRectF &r, const QSizeF &) const
{
    const QPdfSelection sel = m_doc->getSelection(page, r.topLeft(), r.bottomRight());
    if (!sel.isValid() || sel.text().trimmed().isEmpty()) return {};
    QRectF bounds = r;
    if (!sel.bounds().isEmpty())
        bounds = sel.bounds().first().boundingRect();
    return { page, bounds, sel.text() };
}

#endif // HAVE_QT_PDF
