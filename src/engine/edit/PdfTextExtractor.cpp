#ifdef HAVE_QT_PDF

#include "PdfTextExtractor.hpp"
#include <QPdfSelection>
#include <algorithm>
#include <cmath>
#include <numeric>

PdfTextExtractor::PdfTextExtractor(QPdfDocument *doc)
    : m_doc(doc)
{
}

// ── Public ────────────────────────────────────────────────────────────────────

TextBlock PdfTextExtractor::textAt(int page, const QPointF &pdfPt,
                                   const QSizeF &pageSizePts) const
{
    const QPdfSelection all = m_doc->getAllText(page);
    if (!all.isValid()) return {};
    const QList<QPolygonF> &polys = all.bounds();
    if (polys.isEmpty()) return {};

    // Sort indices top-to-bottom by each polygon's top-Y so we can walk
    // adjacent lines for paragraph detection regardless of stream order.
    QVector<int> idx(polys.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        return polys[a].boundingRect().top() < polys[b].boundingRect().top();
    });

    auto lineRect = [&](int si) -> QRectF {
        return polys[idx[si]].boundingRect();
    };

    // ── Find clicked line ─────────────────────────────────────────────────────
    int clickedSorted = -1;

    // Pass 1: strict containment
    for (int i = 0; i < idx.size(); ++i) {
        if (lineRect(i).contains(pdfPt)) { clickedSorted = i; break; }
    }
    // Pass 2: 6pt vertical / 12pt horizontal tolerance
    if (clickedSorted < 0) {
        for (int i = 0; i < idx.size(); ++i) {
            if (lineRect(i).adjusted(-12, -6, 12, 6).contains(pdfPt)) {
                clickedSorted = i; break;
            }
        }
    }
    // Pass 3: nearest line within 48 pts
    if (clickedSorted < 0) {
        qreal bestDist = 48.0;
        for (int i = 0; i < idx.size(); ++i) {
            const QRectF r = lineRect(i);
            const qreal dx = std::max({0.0, r.left() - pdfPt.x(), pdfPt.x() - r.right()});
            const qreal dy = std::max({0.0, r.top()  - pdfPt.y(), pdfPt.y() - r.bottom()});
            const qreal d  = std::hypot(dx, dy);
            if (d < bestDist) { bestDist = d; clickedSorted = i; }
        }
    }
    if (clickedSorted < 0) return {};

    // ── Paragraph grouping ────────────────────────────────────────────────────
    // Group vertically adjacent lines with:
    //   • gap < 1.3 × lineHeight (interline spacing, not a paragraph break)
    //   • left edge within leftTol pts (same text block, not a new column)
    const QRectF refRect  = lineRect(clickedSorted);
    const double leftX    = refRect.left();
    const double lineH    = refRect.height();
    const double maxGap   = lineH * 1.3;
    const double leftTol  = qMax(8.0, lineH * 0.4); // generous: handles minor indents

    auto sameBlock = [&](int si, int siPrev) -> bool {
        const QRectF curr = lineRect(si);
        const QRectF prev = lineRect(siPrev);
        double gap = (si > siPrev)
                   ? curr.top()  - prev.bottom()   // going down
                   : prev.top()  - curr.bottom();   // going up
        if (gap > maxGap) return false;             // paragraph break
        if (gap < -lineH)  return false;            // overlapping = different column
        if (std::abs(curr.left() - leftX) > leftTol) return false;
        return true;
    };

    int firstSorted = clickedSorted;
    for (int i = clickedSorted - 1; i >= 0 && sameBlock(i, i + 1); --i)
        firstSorted = i;

    int lastSorted = clickedSorted;
    for (int i = clickedSorted + 1; i < idx.size() && sameBlock(i, i - 1); ++i)
        lastSorted = i;

    // ── Build combined TextBlock ──────────────────────────────────────────────
    QRectF     combinedBounds;
    QStringList textParts;

    for (int i = firstSorted; i <= lastSorted; ++i) {
        const QRectF r = lineRect(i);
        combinedBounds  = (i == firstSorted) ? r : combinedBounds.united(r);

        const QPdfSelection sel =
            m_doc->getSelection(page, r.topLeft(), r.bottomRight());
        if (sel.isValid()) {
            const QString t = sel.text().trimmed();
            if (!t.isEmpty()) textParts << t;
        }
    }

    if (textParts.isEmpty()) return {};
    // Join lines with '\n' — editor shows multi-line, qpdf draws each line
    return { page, combinedBounds, textParts.join(u'\n') };
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
    const QPdfSelection sel = m_doc->getSelection(page, r.topLeft(), r.bottomRight());
    if (!sel.isValid() || sel.text().trimmed().isEmpty()) return {};

    QRectF bounds = r;
    if (!sel.bounds().isEmpty())
        bounds = sel.bounds().first().boundingRect();

    return { page, bounds, sel.text() };
}

#endif // HAVE_QT_PDF
