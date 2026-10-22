#include "engine/edit/ContentMap.hpp"

#include "engine/edit/TextGutters.hpp"

#include <QDebug>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

bool isListMarker(const QString &word)
{
    static const QRegularExpression numbered(QStringLiteral("^(\\d{1,3}|[a-zA-Z])[.)]$"));
    if (numbered.match(word).hasMatch() || word.startsWith(u'•')) return true;
    // Any single symbol bullet, also a symbol font's from the private use area,
    // but no punctuation, currency or sign that may open a line of running text.
    static const QString kNoMarker = QStringLiteral(".,;:!?'\"()[]{}/\\%&§€$£+=<>#@");
    return word.size() == 1 && !word.at(0).isLetterOrNumber() && !kNoMarker.contains(word.at(0));
}

ResolvedFont resolvePdfFont(const QString &rawBaseFont)
{
    ResolvedFont out;
    if (rawBaseFont.isEmpty()) return out;

    QString n = rawBaseFont;

    const int plus = n.indexOf(u'+');
    if (plus > 0 && plus <= 6) n = n.mid(plus + 1);

    const QString low = n.toLower();
    out.bold   = low.contains(QLatin1String("bold"))
              || low.contains(QLatin1String("black"))
              || low.contains(QLatin1String("heavy"))
              || low.contains(QLatin1String("semibold"))
              || low.contains(QLatin1String("demibold"));
    out.italic = low.contains(QLatin1String("italic"))
              || low.contains(QLatin1String("oblique"));

    int cut = n.indexOf(u'-');
    const int cut2 = n.indexOf(u',');
    if (cut2 >= 0 && (cut < 0 || cut2 < cut)) cut = cut2;
    QString fam = (cut > 0) ? n.left(cut) : n;

    const QString famLow = fam.toLower();
    if (famLow.contains(QLatin1String("helvetica")))     { out.family = QStringLiteral("Helvetica");       return out; }
    if (famLow.contains(QLatin1String("arial")))          { out.family = QStringLiteral("Arial");           return out; }
    if (famLow.contains(QLatin1String("times")))          { out.family = QStringLiteral("Times New Roman"); return out; }
    if (famLow.contains(QLatin1String("courier")))        { out.family = QStringLiteral("Courier New");     return out; }
    if (famLow.contains(QLatin1String("symbol")))         { out.family = QStringLiteral("Symbol");          return out; }
    if (famLow.contains(QLatin1String("zapf")))           { out.family = QStringLiteral("ZapfDingbats");    return out; }

    static const char *kStyleWords[] = {
        "BoldOblique", "BoldItalic", "Bold", "Italic", "Oblique",
        "Black", "Heavy", "Light", "Medium", "SemiBold", "Semibold",
        "DemiBold", "Condensed", "Regular"
    };
    bool again = true;
    while (again) {
        again = false;
        for (const char *w : kStyleWords) {
            const QLatin1String ws(w);
            if (fam.size() > int(ws.size())
                    && fam.endsWith(ws, Qt::CaseInsensitive)) {
                fam.chop(ws.size());
                again = true;
            }
        }
        while (fam.endsWith(u'-') || fam.endsWith(u',') || fam.endsWith(u' '))
            fam.chop(1);
    }

    QString spaced;
    spaced.reserve(fam.size() + 4);
    for (int i = 0; i < fam.size(); ++i) {
        if (i > 0 && fam[i].isUpper() && fam[i - 1].isLower())
            spaced += u' ';
        spaced += fam[i];
    }
    out.family = spaced.trimmed();
    return out;
}

namespace {

struct Seg {
    QRectF  bounds;
    QString text;
    int     firstIdx  { -1 };
    QPointF origin;
    int     words     { 0 };
    double  firstWord { 0.0 };
    bool    marker    { false };
    QRectF  lastWord;
    QRectF  lastObject;
    QHash<QString, int> fonts;
};

struct RowSegs {
    double     y     { 0.0 };
    double     lineH { 0.0 };
    QList<Seg> segs;
    bool       table { false };
};

struct Line {
    ContentItem item;
    double      baseline  { 0.0 };
    double      firstWord { 0.0 };
    bool        marker    { false };
    QString     font;
    bool        oneFont   { true };
    double      prevRight { -1e9 };
    double      nextLeft  { 1e9 };
    bool        entryRow  { false };
};

bool symbolMarker(const QString &marker)
{
    return std::none_of(marker.cbegin(), marker.cend(),
                        [](QChar ch) { return ch.isLetterOrNumber(); });
}

void addWord(Seg &seg, const ContentCluster &c, int index, double lineH)
{
    if (seg.words == 0) {
        seg.firstIdx  = index;
        seg.text      = c.text;
        seg.bounds    = c.bounds;
        seg.origin    = c.origin;
        seg.marker    = isListMarker(c.text.trimmed());
        seg.firstWord = c.bounds.width();
    } else {
        const double gap = c.bounds.left() - seg.bounds.right();
        if (seg.words == 1 && seg.marker) {
            // A list number is set off by a tab; "7. Oktober" in running text is none.
            if (!symbolMarker(seg.text) && gap < lineH * 0.5) seg.marker = false;
            // The word after a list marker is the one that would have to fit,
            // and its font is the line's, not the bullet's.
            else {
                seg.firstWord = c.bounds.width();
                seg.firstIdx  = index;
            }
        }
        if (gap > lineH * 0.18 && !seg.text.endsWith(u' ') && !c.text.startsWith(u' '))
            seg.text += u' ';
        seg.text  += c.text;
        seg.bounds = seg.bounds.united(c.bounds);
    }
    seg.lastWord   = c.bounds;
    seg.lastObject = c.objectBounds;
    seg.fonts[c.rawFontName] += int(c.text.size());
    ++seg.words;
}

QList<RowSegs> segmentRows(const QList<ContentCluster> &cl, bool editing)
{
    // Rows by baseline: a word without capitals keeps to its line when the
    // line of a neighbouring column sits a few points higher.
    struct Row { QList<int> idx; double refY { 0.0 }; double lineH { 0.0 }; };
    const auto lineHeight = [&](int i) {
        return std::max({ cl[i].fontSizePt, cl[i].bounds.height() * 0.8, 5.0 });
    };
    QList<int> order(cl.size());
    for (int i = 0; i < cl.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (cl[a].origin.y() != cl[b].origin.y()) return cl[a].origin.y() < cl[b].origin.y();
        return cl[a].bounds.left() < cl[b].bounds.left();
    });
    QList<Row> rows;
    double start = 0.0;
    for (const int i : std::as_const(order)) {
        if (rows.isEmpty() || cl[i].origin.y() - start > std::max(1.5, lineHeight(i) * 0.3)) {
            rows.append(Row{});
            start = cl[i].origin.y();
        }
        rows.last().idx.append(i);
    }
    // A smaller raised or lowered word, an index or a footnote mark, joins the
    // line it touches.
    for (int r = 0; r < rows.size(); ++r) {
        for (int k = int(rows[r].idx.size()) - 1; k >= 0; --k) {
            const ContentCluster &small = cl[rows[r].idx[k]];
            for (const int n : { r - 1, r + 1 }) {
                if (n < 0 || n >= rows.size()) continue;
                const bool touches = std::any_of(rows[n].idx.cbegin(), rows[n].idx.cend(), [&](int j) {
                    const ContentCluster &big = cl[j];
                    return small.fontSizePt < big.fontSizePt * 0.9
                        && std::abs(small.origin.y() - big.origin.y()) <= big.fontSizePt * 0.6
                        && std::max(small.bounds.left() - big.bounds.right(),
                                    big.bounds.left() - small.bounds.right()) <= big.fontSizePt * 0.3;
                });
                if (!touches) continue;
                rows[n].idx.append(rows[r].idx.takeAt(k));
                break;
            }
        }
    }
    rows.removeIf([](const Row &row) { return row.idx.isEmpty(); });
    for (Row &row : rows) {
        double sum = 0.0;
        for (const int i : std::as_const(row.idx)) {
            sum += cl[i].bounds.center().y();
            row.lineH = std::max(row.lineH, lineHeight(i));
        }
        row.refY = sum / row.idx.size();
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.refY < b.refY; });

    QList<TextGutters::Row> ink;
    for (Row &row : rows) {
        std::sort(row.idx.begin(), row.idx.end(), [&](int a, int b) {
            return cl[a].bounds.left() < cl[b].bounds.left();
        });
        TextGutters::Row r { row.refY, row.lineH, {} };
        for (const int i : std::as_const(row.idx))
            r.ink.append({ cl[i].bounds.left(), cl[i].bounds.right() });
        ink.append(r);
    }

    QList<RowSegs> rsegs;
    rsegs.reserve(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const Row &row = rows.at(r);
        RowSegs rs;
        rs.y     = row.refY;
        rs.lineH = row.lineH;
        Seg cur;
        for (const int i : row.idx) {
            const ContentCluster &c = cl[i];
            if (cur.words > 0) {
                const double gap = c.bounds.left() - cur.bounds.right();
                // A justified narrow column spreads its words wide, so editing
                // splits a line like the backend: at a block gap between text
                // objects, or at a gutter. The tab after a list number is no
                // column, though it looks like one.
                const auto oneObject = [&](const QRectF &object) {
                    return object.contains(cur.lastWord.center()) && object.contains(c.bounds.center());
                };
                const bool apart = !oneObject(cur.lastObject) && !oneObject(c.objectBounds);
                const bool block = editing ? apart && gap > std::max(12.0, row.lineH * 2.5)
                                           : gap >= std::max(row.lineH * 1.5, 10.0);
                if (block
                        || (apart && !(cur.words == 1 && cur.marker)
                            && TextGutters::separates(ink, r, cur.bounds.right(), c.bounds.left()))) {
                    rs.segs.append(cur);
                    cur = Seg{};
                }
            }
            addWord(cur, c, i, row.lineH);
        }
        if (cur.words > 0) rs.segs.append(cur);
        rsegs.append(rs);
    }

    // A list marker set apart from its text by a tab belongs to it.
    for (RowSegs &row : rsegs) {
        if (row.segs.size() < 2 || !row.segs.first().marker
                || row.segs.first().words != 1 || !symbolMarker(row.segs.first().text))
            continue;
        Seg merged = row.segs.first();
        merged.firstIdx  = row.segs[1].firstIdx;
        merged.firstWord = row.segs[1].firstWord;
        merged.words     = 2;
        merged.fonts.clear();
        for (int n = 1; n < row.segs.size(); ++n) {
            merged.text += u' ' + row.segs[n].text;
            merged.bounds = merged.bounds.united(row.segs[n].bounds);
            for (auto it = row.segs[n].fonts.cbegin(); it != row.segs[n].fonts.cend(); ++it)
                merged.fonts[it.key()] += it.value();
        }
        row.segs = { merged };
    }
    return rsegs;
}

void markTables(QList<RowSegs> &rsegs, double reach)
{
    for (int i = 0; i < rsegs.size(); ++i) {
        if (rsegs[i].segs.size() < 2) continue;
        for (int j = i + 1; j < rsegs.size(); ++j) {
            const double dy = rsegs[j].y - rsegs[i].y;
            if (dy > std::max(rsegs[i].lineH, rsegs[j].lineH) * reach) break;
            if (rsegs[j].segs.size() < 2) continue;
            const double tol = std::max(rsegs[i].lineH, rsegs[j].lineH) * 1.2;
            int aligned = 0;
            for (const Seg &si : rsegs[i].segs)
                for (const Seg &sj : rsegs[j].segs)
                    if (std::abs(si.bounds.left() - sj.bounds.left()) < tol) {
                        ++aligned;
                        break;
                    }
            if (aligned >= 2) {
                rsegs[i].table = true;
                rsegs[j].table = true;
            }
        }
    }
}

double sizeOf(const Line &a, const Line &b)
{
    return std::max({ a.item.fontSizePt, b.item.fontSizePt, 6.0 });
}

bool continuesBelow(const Line &p, const Line &l)
{
    const double fs = sizeOf(p, l);
    const double step = l.baseline - p.baseline;
    if (step < fs * 0.5 || step > fs * 2.6) return false;
    if (l.item.bounds.top() - p.item.bounds.bottom() < -fs * 0.4) return false;
    if (p.item.fontSizePt > 0.0 && l.item.fontSizePt > 0.0
            && std::min(p.item.fontSizePt, l.item.fontSizePt)
                   / std::max(p.item.fontSizePt, l.item.fontSizePt) < 0.75)
        return false;
    // Lines are compared by family, so a bold or italic word at a line start stays.
    if (!p.item.rawFontName.isEmpty() && !l.item.rawFontName.isEmpty()
            && p.item.fontFamily != l.item.fontFamily)
        return false;
    const QRectF &a = p.item.bounds;
    const QRectF &b = l.item.bounds;
    if (std::min(a.right(), b.right()) - std::max(a.left(), b.left()) < 1.0) return false;
    // Neither may reach into the column or cell beside the other.
    if (a.right() > l.nextLeft - 1.0 || b.right() > p.nextLeft - 1.0
            || a.left() < l.prevRight + 1.0 || b.left() < p.prevRight + 1.0)
        return false;
    return std::abs(a.left() - b.left()) <= fs * 4.0
        || std::abs(a.center().x() - b.center().x()) <= fs;
}

// Lines that run on below each other, one chain per column, cell or centred block.
QList<QList<int>> chainLines(const QList<Line> &lines)
{
    QList<QList<int>> chains;
    for (int i = 0; i < lines.size(); ++i) {
        int best = -1;
        double nearest = 0.0;
        for (int k = 0; k < chains.size(); ++k) {
            const Line &p = lines[chains[k].last()];
            if (!continuesBelow(p, lines[i])) continue;
            const double step = lines[i].baseline - p.baseline;
            const double offset = std::abs(p.item.bounds.left() - lines[i].item.bounds.left());
            if (best < 0 || step + offset * 0.01 < nearest) {
                best = k;
                nearest = step + offset * 0.01;
            }
        }
        if (best < 0) chains.append(QList<int>{ i });
        else chains[best].append(i);
    }
    return chains;
}

void addParagraphs(const QList<Line> &lines, const QList<int> &chain, double lineStep,
                   QList<ContentItem> &out)
{
    // The chain is set flush left (with indents) unless its neighbouring lines
    // agree more often in their centres or right ends than in their starts.
    int lefts = 0, centres = 0, rights = 0, flush = 0;
    for (int k = 1; k < chain.size(); ++k) {
        const QRectF &a = lines[chain[k - 1]].item.bounds;
        const QRectF &b = lines[chain[k]].item.bounds;
        const double tol = sizeOf(lines[chain[k - 1]], lines[chain[k]]) * 0.6;
        lefts   += std::abs(a.left() - b.left()) <= tol;
        centres += std::abs(a.center().x() - b.center().x()) <= tol;
        rights  += std::abs(a.right() - b.right()) <= tol;
        flush   += std::abs(a.right() - b.right()) <= 1.0;
    }
    const bool leftAligned = lefts >= std::max(centres, rights);

    // Justified: lines end on the margin to a point, all but a paragraph's last.
    const bool justified = leftAligned && flush >= std::max(2.0, (chain.size() - 1) * 0.3);
    ContentItem para = lines[chain[0]].item;
    double paraLeft = para.bounds.left();
    double paraWidest = para.bounds.width();
    bool listItem = lines[chain[0]].marker;
    int count = 1;
    double stepSum = 0.0;
    const auto finish = [&]() {
        if (count > 1) {
            para.lineSpacingPt = stepSum / (count - 1);
            if (para.type == ContentItem::Type::Text) para.type = ContentItem::Type::Paragraph;
        }
        out.append(para);
    };
    for (int k = 1; k < chain.size(); ++k) {
        const Line &p = lines[chain[k - 1]];
        const Line &l = lines[chain[k]];
        const double fs = sizeOf(p, l);
        const double step = l.baseline - p.baseline;
        const QRectF &pb = p.item.bounds;
        const QRectF &lb = l.item.bounds;
        // The first word of the line would have fit at the end of the one before;
        // the edge is taken from the lines around, so an indented quote keeps its own.
        const QRectF next = k + 1 < chain.size() ? lines[chain[k + 1]].item.bounds : lb;
        const double right = std::max({ para.bounds.right(), lb.right(), next.right() });
        const double widest = std::max({ paraWidest, lb.width(), next.width() });
        const double room = leftAligned ? right - pb.right() : widest - pb.width();
        // Space between paragraphs: a step wider than the paragraph's own line
        // step, or for its first line wider than the step that follows.
        double usual = count > 1 ? stepSum / (count - 1)
                     : k + 1 < chain.size() ? lines[chain[k + 1]].baseline - l.baseline
                     : 0.0;
        double slack = std::max(fs * 0.3, 2.5);
        // Short cells give no width to test against; a step beyond the page's
        // plain line step there is the padding of the next table row.
        if (lineStep > 0.0 && (p.item.type == ContentItem::Type::TableCell
                               || l.item.type == ContentItem::Type::TableCell)) {
            usual = usual > 0.0 ? std::min(usual, lineStep) : lineStep;
            slack = 1.0;
        }
        // A line set wholly in one font (a heading) before text mostly in another.
        const bool heading = count == 1 && p.oneFont && !p.font.isEmpty() && !l.font.isEmpty()
                          && p.font != l.font;
        // In justified text a line of several words that stops short of the
        // margin is a paragraph's last, even when the next word would not fit.
        const bool shortOfMargin = justified && p.item.text.contains(u' ')
                                && right - pb.right() > std::max(1.5, fs * 0.15);
        // In a table of short entries every row starts new cells.
        const bool nextRow = p.entryRow && l.entryRow;
        bool ends = l.marker || heading || shortOfMargin || nextRow
                 || room >= fs * 0.25 + l.firstWord - 0.5
                 || (usual > 0.0 && step > usual * 1.15 + slack);
        if (!ends && leftAligned) {
            const double shift = lb.left() - pb.left();
            if (count == 1)
                // An indented first line, or the text after a list marker.
                ends = shift > fs * 0.5 && !(listItem && shift <= fs * 3.0);
            else
                ends = std::abs(lb.left() - paraLeft) > fs * 0.6;
        }
        if (ends) {
            finish();
            para = l.item;
            paraLeft = lb.left();
            paraWidest = lb.width();
            listItem = l.marker;
            count = 1;
            stepSum = 0.0;
            continue;
        }
        if (count == 1) paraLeft = lb.left();
        paraWidest  = std::max(paraWidest, lb.width());
        para.bounds = para.bounds.united(lb);
        para.text  += u'\n' + l.item.text;
        stepSum    += step;
        ++count;
    }
    finish();
}

QList<ContentItem> joinParagraphs(QList<Line> lines)
{
    std::sort(lines.begin(), lines.end(), [](const Line &a, const Line &b) {
        if (std::abs(a.baseline - b.baseline) > 0.5) return a.baseline < b.baseline;
        return a.item.bounds.left() < b.item.bounds.left();
    });
    // The plain line step inside cells: the smallest step down from a cell.
    const QList<QList<int>> chains = chainLines(lines);
    double lineStep = 0.0;
    for (const QList<int> &chain : chains)
        for (int k = 1; k < chain.size(); ++k) {
            if (lines[chain[k - 1]].item.type != ContentItem::Type::TableCell) continue;
            const double step = lines[chain[k]].baseline - lines[chain[k - 1]].baseline;
            if (lineStep <= 0.0 || step < lineStep) lineStep = step;
        }
    QList<ContentItem> out;
    for (const QList<int> &chain : chains)
        addParagraphs(lines, chain, lineStep, out);
    std::sort(out.begin(), out.end(), [](const ContentItem &a, const ContentItem &b) {
        if (std::abs(a.bounds.top() - b.bounds.top()) > 0.5)
            return a.bounds.top() < b.bounds.top();
        return a.bounds.left() < b.bounds.left();
    });
    return out;
}

}

QList<ContentItem> classifyContentClusters(QList<ContentCluster> clusters,
                                           bool mergeVertical)
{
    QList<ContentCluster> cl;
    cl.reserve(clusters.size());
    for (const ContentCluster &c : clusters)
        // Only a cluster without any extent is dropped. A row of underscores in
        // a form field is 0.39 pt of ink and would fail a height threshold,
        // although it is as much text as the line above it.
        if (!c.text.trimmed().isEmpty()
                && c.bounds.width() > 0.0 && c.bounds.height() > 0.0)
            cl.append(c);
    if (cl.isEmpty()) return {};

    std::sort(cl.begin(), cl.end(), [](const ContentCluster &a, const ContentCluster &b) {
        if (std::abs(a.bounds.center().y() - b.bounds.center().y()) > 3.0)
            return a.bounds.center().y() < b.bounds.center().y();
        return a.bounds.left() < b.bounds.left();
    });

    QList<RowSegs> rsegs = segmentRows(cl, mergeVertical);
    markTables(rsegs, mergeVertical ? 2.4 : 3.4);

    QList<Line> lines;
    for (const RowSegs &rs : rsegs) {
        // A row of short entries: each takes at most a third of the room to the next.
        bool entryRow = rs.segs.size() >= 2;
        for (int n = 0; n + 1 < rs.segs.size(); ++n)
            if (rs.segs.at(n).bounds.width()
                    > (rs.segs.at(n + 1).bounds.left() - rs.segs.at(n).bounds.left()) * 0.35)
                entryRow = false;
        for (int n = 0; n < rs.segs.size(); ++n) {
            const Seg &seg = rs.segs.at(n);
            const ContentCluster &src = cl[seg.firstIdx];
            ContentItem it;
            it.type        = (rs.table && rs.segs.size() >= 2)
                                 ? ContentItem::Type::TableCell
                                 : ContentItem::Type::Text;
            it.bounds        = seg.bounds;
            it.textOrigin    = seg.origin;
            it.text          = seg.text.trimmed();
            it.fontSizePt    = src.fontSizePt;
            it.fontSizeExact = src.fontSizeExact;
            it.rawFontName   = src.rawFontName;
            it.textColor     = src.textColor;
            const ResolvedFont rf = resolvePdfFont(src.rawFontName);
            it.fontFamily    = rf.family;
            it.bold          = rf.bold;
            it.italic        = rf.italic;
            QString font;
            for (auto f = seg.fonts.cbegin(); f != seg.fonts.cend(); ++f)
                if (font.isNull() || f.value() > seg.fonts.value(font)) font = f.key();
            if (it.isValid() && !it.text.isEmpty())
                lines.append({ std::move(it), seg.origin.y(), seg.firstWord, seg.marker, font,
                               seg.fonts.size() <= 1,
                               n > 0 ? rs.segs.at(n - 1).bounds.right() : -1e9,
                               n + 1 < rs.segs.size() ? rs.segs.at(n + 1).bounds.left() : 1e9,
                               entryRow });
        }
    }

    if (mergeVertical) return joinParagraphs(std::move(lines));

    QList<ContentItem> items;
    for (Line &line : lines) items.append(std::move(line.item));
    std::sort(items.begin(), items.end(), [](const ContentItem &a, const ContentItem &b) {
        if (std::abs(a.bounds.top() - b.bounds.top()) > 0.5)
            return a.bounds.top() < b.bounds.top();
        return a.bounds.left() < b.bounds.left();
    });
    return items;
}

ContentItem contentItemAt(const QList<ContentItem> &items, const QPointF &pdfPt,
                          unsigned typeMask, double maxDistance)
{
    const auto prio = [](ContentItem::Type t) -> int {
        switch (t) {
        case ContentItem::Type::FormField: return 3;
        case ContentItem::Type::TableCell: return 2;
        case ContentItem::Type::Paragraph:
        case ContentItem::Type::Text:      return 1;
        default:                           return 0;
        }
    };

    const ContentItem *best = nullptr;
    int    bestPrio = -1;
    double bestArea = 1e18;
    for (const ContentItem &item : items) {
        if (!(typeMask & contentTypeBit(item.type))) continue;
        if (!item.bounds.adjusted(-3, -3, 3, 3).contains(pdfPt)) continue;
        const int    p    = prio(item.type);
        const double area = item.bounds.width() * item.bounds.height();
        if (p > bestPrio || (p == bestPrio && area < bestArea)) {
            best = &item; bestPrio = p; bestArea = area;
        }
    }
    if (best) return *best;

    double bestDist = maxDistance;
    for (const ContentItem &item : items) {
        if (!(typeMask & contentTypeBit(item.type))) continue;
        const double dx = std::max(0.0, std::max(item.bounds.left() - pdfPt.x(),
                                                 pdfPt.x() - item.bounds.right()));
        const double dy = std::max(0.0, std::max(item.bounds.top() - pdfPt.y(),
                                                 pdfPt.y() - item.bounds.bottom()));
        double dist = std::hypot(dx, dy);
        if (item.type == ContentItem::Type::FormField)      dist *= 0.5;
        else if (item.type == ContentItem::Type::TableCell) dist *= 0.8;
        if (dist < bestDist) { bestDist = dist; best = &item; }
    }
    return best ? *best : ContentItem{};
}
