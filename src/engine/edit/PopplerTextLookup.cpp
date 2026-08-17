#include "engine/edit/PopplerTextLookup.hpp"

#ifdef HAVE_POPPLER

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// A word whose centre lies in an erased region is treated as if it were not
// on the page at all.
bool excluded(const QRectF &glyph, const QList<QRectF> &exclude)
{
    for (const QRectF &e : exclude)
        if (e.contains(glyph.center())) return true;
    return false;
}

struct PopplerWord { QRectF bbox; QString text; bool spaceAfter; };

// Collects the words of the text block covering `area`, then COMPLETES each
// line: the region model's width estimates can end short of the real line,
// so collected lines grow word-by-word over word-sized gaps until the true
// line ends. Column gutters (gaps far wider than a word space) are never
// crossed. Incomplete collection here caused half-erased originals after a
// move — line-end words missing from the erase rects stayed visible.
std::vector<PopplerWord> collectBlockWords(Poppler::Document *doc, int page,
                                           const QRectF &area,
                                           const QList<QRectF> &exclude)
{
    std::vector<PopplerWord> all;
    try {
        auto popplerPage = doc->page(page);
        if (!popplerPage) return {};
        for (const auto &tb : popplerPage->textList()) {
            const QRectF b = tb->boundingBox();
            if (!b.isEmpty() && !excluded(b, exclude))
                all.push_back({ b, tb->text(), tb->hasSpaceAfter() });
        }
    } catch (...) { return {}; }

    std::vector<char> used(all.size(), 0);
    const QRectF seedArea = area.adjusted(-2, -2, 2, 2);
    bool any = false;
    for (size_t i = 0; i < all.size(); ++i)
        if (seedArea.contains(all[i].bbox.center())) { used[i] = 1; any = true; }
    if (!any) return {};

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < all.size(); ++i) {
            if (used[i]) continue;
            const QRectF &w = all[i].bbox;
            for (size_t j = 0; j < all.size(); ++j) {
                if (!used[j]) continue;
                const QRectF &u = all[j].bbox;
                const double lineH = std::max({ u.height(), w.height(), 4.0 });
                if (std::abs(w.center().y() - u.center().y()) > lineH * 0.6)
                    continue;
                const double gap = (w.left() >= u.right())
                                       ? w.left() - u.right()
                                       : u.left() - w.right();
                if (gap < lineH * 1.2) { used[i] = 1; changed = true; break; }
            }
        }
    }

    std::vector<PopplerWord> out;
    for (size_t i = 0; i < all.size(); ++i)
        if (used[i]) out.push_back(all[i]);
    std::sort(out.begin(), out.end(),
              [](const PopplerWord &a, const PopplerWord &b) {
        if (std::abs(a.bbox.center().y() - b.bbox.center().y()) > 3.0)
            return a.bbox.center().y() < b.bbox.center().y();
        return a.bbox.left() < b.bbox.left();
    });
    return out;
}

} // namespace

namespace PopplerText {

TextBlock textAt(Poppler::Document *doc, int page, const QPointF &pdfPt,
                 const QList<QRectF> &exclude)
{
    // Poppler can throw on malformed files — degrade to "no text found".
    try {
    auto popplerPage = doc->page(page);
    if (!popplerPage) return {};

    const auto textBoxes = popplerPage->textList();
    if (textBoxes.empty()) return {};

    // Find the nearest word to the click point (exact hit preferred).
    double bestDist = 40.0;
    int bestIdx = -1;
    for (int i = 0; i < static_cast<int>(textBoxes.size()); ++i) {
        const QRectF bbox = textBoxes[i]->boundingBox();
        if (excluded(bbox, exclude)) continue;
        if (bbox.contains(pdfPt)) { bestIdx = i; bestDist = 0.0; break; }
        const double dx = qMax(0.0, qMax(bbox.left() - pdfPt.x(), pdfPt.x() - bbox.right()));
        const double dy = qMax(0.0, qMax(bbox.top()  - pdfPt.y(), pdfPt.y() - bbox.bottom()));
        const double d  = std::hypot(dx, dy);
        if (d < bestDist) { bestDist = d; bestIdx = i; }
    }
    if (bestIdx < 0) return {};

    const QRectF anchor = textBoxes[bestIdx]->boundingBox();
    const double lineY  = anchor.center().y();
    const double lineH  = qMax(anchor.height(), 6.0);

    // Collect all words on the same line (within lineH * 0.6 of anchor centre Y)
    struct Word { QRectF bbox; QString text; bool spaceAfter; };
    std::vector<Word> lineWords;
    for (const auto &tb : textBoxes) {
        const QRectF bbox = tb->boundingBox();
        if (excluded(bbox, exclude)) continue;
        if (std::abs(bbox.center().y() - lineY) < lineH * 0.6)
            lineWords.push_back({ bbox, tb->text(), tb->hasSpaceAfter() });
    }
    if (lineWords.empty()) return {};

    std::sort(lineWords.begin(), lineWords.end(),
              [](const Word &a, const Word &b) { return a.bbox.left() < b.bbox.left(); });

    QRectF lineRect;
    QString lineText;
    for (int i = 0; i < static_cast<int>(lineWords.size()); ++i) {
        lineRect = (i == 0) ? lineWords[i].bbox : lineRect.united(lineWords[i].bbox);
        if (i > 0 && lineWords[i - 1].spaceAfter)
            lineText += QLatin1Char(' ');
        lineText += lineWords[i].text;
    }

    return lineText.isEmpty() ? TextBlock{} : TextBlock{ page, lineRect, lineText };
    } catch (...) { return {}; }
}

TextBlock blockInRect(Poppler::Document *doc, int page, const QRectF &rect,
                      const QList<QRectF> &exclude)
{
    const auto words = collectBlockWords(doc, page, rect, exclude);
    if (words.empty()) return {};

    QString out;
    QRectF  united = words.front().bbox;
    double  lineY  = words.front().bbox.center().y();
    double  lineH  = qMax(words.front().bbox.height(), 6.0);
    for (size_t i = 0; i < words.size(); ++i) {
        const PopplerWord &w = words[i];
        united = united.united(w.bbox);
        if (i > 0) {
            if (std::abs(w.bbox.center().y() - lineY) > lineH * 0.6) {
                out += u'\n';
                lineY = w.bbox.center().y();
                lineH = qMax(w.bbox.height(), 6.0);
            } else if (words[i - 1].spaceAfter) {
                out += u' ';
            }
        }
        out += w.text;
    }
    out = out.trimmed();
    return out.isEmpty() ? TextBlock{} : TextBlock{ page, united, out };
}

QList<QRectF> glyphRects(Poppler::Document *doc, int page, const QRectF &area,
                         const QList<QRectF> &exclude)
{
    QList<QRectF> out;
    for (const PopplerWord &w : collectBlockWords(doc, page, area, exclude))
        out.append(w.bbox);
    return out;
}

} // namespace PopplerText

#endif // HAVE_POPPLER
