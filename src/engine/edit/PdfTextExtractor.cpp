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

    // Sort top-to-bottom; within the same visual line (top-Y within 2pt), sort
    // left-to-right.  Qt PDF can split one visual line into multiple polygons at
    // font / encoding boundaries — the secondary x-sort puts them in reading order.
    QVector<int> idx(polys.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        const QRectF ra = polys[a].boundingRect();
        const QRectF rb = polys[b].boundingRect();
        if (std::abs(ra.top() - rb.top()) < 2.0)
            return ra.left() < rb.left();
        return ra.top() < rb.top();
    });

    auto lineRect = [&](int si) -> QRectF {
        return polys[idx[si]].boundingRect();
    };

    // Precompute visual-line group: polygons that overlap in Y (gap < 0 from the
    // one before) are on the same visual line and share a group index.
    const int n = idx.size();
    QVector<int> lineGroup(n, 0);
    for (int i = 1; i < n; ++i) {
        const qreal gap = lineRect(i).top() - lineRect(i - 1).bottom();
        lineGroup[i] = lineGroup[i - 1] + (gap >= 0 ? 1 : 0);
    }

    // ── Find clicked line ─────────────────────────────────────────────────────
    int clickedSorted = -1;

    // Pass 1: strict containment
    for (int i = 0; i < n; ++i) {
        if (lineRect(i).contains(pdfPt)) { clickedSorted = i; break; }
    }
    // Pass 2: 6pt vertical / 12pt horizontal tolerance
    if (clickedSorted < 0) {
        for (int i = 0; i < n; ++i) {
            if (lineRect(i).adjusted(-12, -6, 12, 6).contains(pdfPt)) {
                clickedSorted = i; break;
            }
        }
    }
    // Pass 3: nearest line within 48 pts
    if (clickedSorted < 0) {
        qreal bestDist = 48.0;
        for (int i = 0; i < n; ++i) {
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
    //   • gap < 1.3 × lineHeight (not a paragraph break)
    //   • left edge within leftTol pts (same column)
    // Exception: polygons that Y-overlap (same visual line, gap < 0) are ALWAYS
    // merged regardless of their left edge — they are split text runs of one line.
    const QRectF refRect  = lineRect(clickedSorted);
    const double leftX    = refRect.left();
    const double lineH    = refRect.height();
    const double maxGap   = lineH * 1.3;
    const double leftTol  = qMax(8.0, lineH * 0.4);

    auto sameBlock = [&](int si, int siPrev) -> bool {
        const QRectF curr = lineRect(si);
        const QRectF prev = lineRect(siPrev);
        const double gap = (si > siPrev)
                         ? curr.top()  - prev.bottom()
                         : prev.top()  - curr.bottom();
        if (gap > maxGap) return false;          // paragraph break
        // Same visual line (overlapping Y): always merge.  Qt PDF splits a single
        // line into multiple polygons at Tf/Tj boundaries; the leftTol guard must
        // not be applied here or trailing fragments ("ch" at x=420 while leftX=58)
        // would be left out of the block and remain visible after a blank-fill.
        if (gap < 0) return true;
        // Adjacent line: apply column / indent guard.
        if (std::abs(curr.left() - leftX) > leftTol) return false;
        return true;
    };

    int firstSorted = clickedSorted;
    for (int i = clickedSorted - 1; i >= 0 && sameBlock(i, i + 1); --i)
        firstSorted = i;

    int lastSorted = clickedSorted;
    for (int i = clickedSorted + 1; i < n && sameBlock(i, i - 1); ++i)
        lastSorted = i;

    // ── Build combined TextBlock ──────────────────────────────────────────────
    QRectF      combinedBounds;
    QStringList textParts;   // one entry per visual line; same-line fragments appended

    for (int i = firstSorted; i <= lastSorted; ++i) {
        const QRectF r = lineRect(i);
        combinedBounds = (i == firstSorted) ? r : combinedBounds.united(r);

        const QPdfSelection sel =
            m_doc->getSelection(page, r.topLeft(), r.bottomRight());
        if (sel.isValid()) {
            const QString t = sel.text().trimmed();
            if (!t.isEmpty()) {
                // If this polygon is on the same visual line as the previous one,
                // append its text (no separator — the words already have correct
                // spacing from the PDF).  Otherwise start a new '\n'-joined entry.
                const bool sameLine = (i > firstSorted)
                                   && (lineGroup[i] == lineGroup[i - 1]);
                if (sameLine && !textParts.isEmpty())
                    textParts.last() += t;
                else
                    textParts << t;
            }
        }
    }

    if (textParts.isEmpty()) return {};
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
