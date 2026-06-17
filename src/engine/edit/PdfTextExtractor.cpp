#ifdef HAVE_QT_PDF

#include "PdfTextExtractor.hpp"
#include <QPdfSelection>

PdfTextExtractor::PdfTextExtractor(QPdfDocument *doc)
    : m_doc(doc)
{}

TextBlock PdfTextExtractor::textAt(int page, const QPointF &pdfPt,
                                   const QSizeF &pageSizePts) const
{
    const QPdfSelection all = m_doc->getAllText(page);
    if (!all.isValid()) return {};

    const QList<QPolygonF> &polys = all.bounds();

    for (const QPolygonF &poly : polys) {
        const QRectF r = poly.boundingRect();
        if (r.contains(pdfPt)) {
            TextBlock blk = fetchBlock(page, r, pageSizePts);
            if (blk.isValid()) return blk;
        }
    }

    for (const QPolygonF &poly : polys) {
        const QRectF r = poly.boundingRect().adjusted(0, -4, 0, 4);
        if (r.contains(pdfPt)) {
            TextBlock blk = fetchBlock(page, poly.boundingRect(), pageSizePts);
            if (blk.isValid()) return blk;
        }
    }

    const QPolygonF *best = nullptr;
    qreal bestDist = 12.0;
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

#elif defined(HAVE_POPPLER)

#include "PdfTextExtractor.hpp"
#include <algorithm>

PdfTextExtractor::PdfTextExtractor(Poppler::Document *doc)
    : m_doc(doc)
{}

// Poppler text boxes use PDF native coordinates: origin bottom-left, Y upward.
// We convert them to screen coordinates (top-left, Y downward) to match
// the click positions our callers pass in.
QList<TextBlock> PdfTextExtractor::buildBlocks(int page) const
{
    auto p = m_doc->page(page);
    if (!p) return {};

    const qreal pageH = p->pageSizeF().height();
    auto words = p->textList();

    // Convert each word to screen coords and store with its original word info
    struct Word {
        QRectF  screenBox;
        QString text;
        bool    spaceAfter;
    };
    std::vector<Word> screenWords;
    screenWords.reserve(words.size());
    for (const auto &w : words) {
        const QRectF b = w->boundingBox();
        // PDF→screen: flip Y (PDF origin is bottom-left, Y up)
        const QRectF sBox(b.x(), pageH - b.y() - b.height(), b.width(), b.height());
        screenWords.push_back({ sBox, w->text(), w->hasSpaceAfter() });
    }

    // Sort top-to-bottom, left-to-right
    std::sort(screenWords.begin(), screenWords.end(), [](const Word &a, const Word &b) {
        if (qAbs(a.screenBox.top() - b.screenBox.top()) > 2.0)
            return a.screenBox.top() < b.screenBox.top();
        return a.screenBox.left() < b.screenBox.left();
    });

    // Group words with overlapping Y-ranges into line blocks
    QList<TextBlock> blocks;
    int i = 0;
    while (i < (int)screenWords.size()) {
        QRectF  lineBox  = screenWords[i].screenBox;
        QString lineText = screenWords[i].text;
        int j = i + 1;
        while (j < (int)screenWords.size()) {
            const QRectF &next = screenWords[j].screenBox;
            if (next.top() < lineBox.bottom() + 2.0) {
                if (screenWords[j - 1].spaceAfter)
                    lineText += QLatin1Char(' ');
                lineText += screenWords[j].text;
                lineBox = lineBox.united(next);
                ++j;
            } else {
                break;
            }
        }
        if (!lineText.trimmed().isEmpty())
            blocks.append({ page, lineBox, lineText.trimmed() });
        i = j;
    }
    return blocks;
}

TextBlock PdfTextExtractor::textAt(int page, const QPointF &pdfPt,
                                   const QSizeF & /*pageSizePts*/) const
{
    const QList<TextBlock> blocks = buildBlocks(page);

    for (const TextBlock &blk : blocks)
        if (blk.pdfBounds.contains(pdfPt)) return blk;

    for (const TextBlock &blk : blocks)
        if (blk.pdfBounds.adjusted(0, -4, 0, 4).contains(pdfPt)) return blk;

    const TextBlock *best = nullptr;
    qreal bestDist = 12.0;
    for (const TextBlock &blk : blocks) {
        const qreal dy = (pdfPt.y() < blk.pdfBounds.top())    ? blk.pdfBounds.top()    - pdfPt.y()
                       : (pdfPt.y() > blk.pdfBounds.bottom())  ? pdfPt.y() - blk.pdfBounds.bottom()
                       : 0;
        if (dy < bestDist) { bestDist = dy; best = &blk; }
    }
    if (best) return *best;

    return {};
}

QList<TextBlock> PdfTextExtractor::allBlocks(int page) const
{
    return buildBlocks(page);
}

#endif // HAVE_QT_PDF / HAVE_POPPLER
