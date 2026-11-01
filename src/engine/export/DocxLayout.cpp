#include "engine/export/DocxLayout.hpp"
#include "engine/export/DocxXml.hpp"

#include <QHash>
#include <QPainter>
#include <QSet>
#include <QStack>
#include <QtMath>

#include <algorithm>
#include <cstdio>
#include <cmath>

static bool layoutDebug()
{
    static const bool on = !qEnvironmentVariable("OPENPDF_DEBUG_LAYOUT").isEmpty();
    return on;
}

namespace {

/// Provides opaque pixel access and PDF-point conversion for a page render.
class Raster
{
public:
    Raster() = default;
    Raster(const QImage &src, qreal scale) : m_scale(scale)
    {
        if (src.isNull()) return;
        m_img = QImage(src.size(), QImage::Format_RGB32);
        m_img.fill(Qt::white);
        QPainter p(&m_img);
        p.drawImage(0, 0, src);
        p.end();
    }

    bool  isNull() const { return m_img.isNull(); }
    int   width()  const { return m_img.width(); }
    int   height() const { return m_img.height(); }
    qreal scale()  const { return m_scale; }

    QRgb at(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= m_img.width() || y >= m_img.height())
            return qRgb(255, 255, 255);
        return m_img.pixel(x, y);
    }

    QRect toPx(const QRectF &pt) const
    {
        return QRect(QPoint(qFloor(pt.left()   * m_scale), qFloor(pt.top()    * m_scale)),
                     QPoint(qCeil (pt.right()  * m_scale), qCeil (pt.bottom() * m_scale)))
               .intersected(m_img.rect());
    }

    QRectF toPt(const QRect &px) const
    {
        return QRectF(px.left() / m_scale, px.top() / m_scale,
                      px.width() / m_scale, px.height() / m_scale);
    }

    QImage crop(const QRect &px) const { return m_img.copy(px.intersected(m_img.rect())); }

private:
    QImage m_img;
    qreal  m_scale { 1.0 };
};

inline int colorDist(QRgb a, QRgb b)
{
    return qAbs(qRed(a)   - qRed(b))
         + qAbs(qGreen(a) - qGreen(b))
         + qAbs(qBlue(a)  - qBlue(b));
}

QRgb paperColor(const Raster &r)
{
    if (r.isNull()) return qRgb(255, 255, 255);
    QHash<QRgb, int> counts;
    const int stepX = qMax(1, r.width() / 60);
    const int stepY = qMax(1, r.height() / 80);
    for (int y = 0; y < r.height(); y += stepY)
        for (int x = 0; x < r.width(); x += stepX)
            ++counts[r.at(x, y)];
    QRgb best = qRgb(255, 255, 255);
    int bestN = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() > bestN) { bestN = it.value(); best = it.key(); }
    return best;
}

QColor dominantFill(const Raster &r, QRgb paper, const QRectF &area,
                    double insetPt = 1.2)
{
    if (r.isNull()) return {};
    QRect box = r.toPx(area.adjusted(insetPt, insetPt, -insetPt, -insetPt));
    if (box.isEmpty()) box = r.toPx(area);
    if (box.isEmpty()) return {};

    QHash<QRgb, int> counts;
    const int step = qMax(1, qRound(r.scale()));
    int total = 0;
    for (int y = box.top(); y <= box.bottom(); y += step) {
        for (int x = box.left(); x <= box.right(); x += step) {
            const QRgb c = r.at(x, y);
            const auto quantise = [](int channel) {
                return qMin(255, ((channel + 4) / 8) * 8);
            };
            const QRgb bucket = qRgb(quantise(qRed(c)), quantise(qGreen(c)),
                                     quantise(qBlue(c)));
            ++counts[bucket];
            ++total;
        }
    }
    QRgb best = paper;
    int bestN = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() > bestN) { bestN = it.value(); best = it.key(); }
    if (total == 0 || double(bestN) / total < 0.40
            || colorDist(best, paper) < 30)
        return {};
    return QColor::fromRgb(best);
}

QColor dominantInkColor(const Raster &r, QRgb paper, const QRectF &area)
{
    if (r.isNull()) return {};
    const QRect box = r.toPx(area);
    if (box.isEmpty()) return {};
    QHash<QRgb, int> counts;
    const int step = qMax(1, qRound(r.scale() / 2.0));
    for (int y = box.top(); y <= box.bottom(); y += step) {
        for (int x = box.left(); x <= box.right(); x += step) {
            const QRgb c = r.at(x, y);
            if (colorDist(c, paper) < 30) continue;
            const auto quantise = [](int channel) {
                return qMin(255, ((channel + 4) / 8) * 8);
            };
            ++counts[qRgb(quantise(qRed(c)), quantise(qGreen(c)),
                          quantise(qBlue(c)))];
        }
    }
    QRgb best = paper;
    int bestN = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() > bestN) { bestN = it.value(); best = it.key(); }
    return bestN > 0 ? QColor::fromRgb(best) : QColor{};
}

double median(QList<double> v)
{
    if (v.isEmpty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double fontSizeOf(const ContentItem &item)
{
    return item.fontSizePt > 0.0 ? item.fontSizePt
                                 : qMax(6.0, item.bounds.height() * 0.9);
}

bool sameTextStyle(const ContentItem &a, const ContentItem &b)
{
    if (a.bold != b.bold || a.italic != b.italic) return false;
    if (!a.fontFamily.isEmpty() && !b.fontFamily.isEmpty()
            && a.fontFamily != b.fontFamily)
        return false;
    if (a.textColor.isValid() && b.textColor.isValid()
            && colorDist(a.textColor.rgb(), b.textColor.rgb()) > 40)
        return false;
    const double fa = fontSizeOf(a), fb = fontSizeOf(b);
    return qMin(fa, fb) / qMax(fa, fb) > 0.86;
}

struct RawTable {
    QList<ContentItem> items;
    QRectF bounds;

    bool   measured { false };
};

QList<QList<ContentItem>> clusterRows(QList<ContentItem> items)
{
    std::sort(items.begin(), items.end(),
              [](const ContentItem &a, const ContentItem &b) {
        if (qAbs(a.bounds.center().y() - b.bounds.center().y()) > 1.5)
            return a.bounds.center().y() < b.bounds.center().y();
        return a.bounds.left() < b.bounds.left();
    });
    QList<QList<ContentItem>> rows;
    for (const ContentItem &item : items) {
        if (!rows.isEmpty()) {
            const QList<ContentItem> &row = rows.last();
            const double lineH = row.first().bounds.height();
            const double tol   = qMax(lineH, item.bounds.height()) * 0.6;

            // An underline is a fraction of a line high and sits just below the
            // text it belongs to, further away than the usual tolerance allows.
            // Measured from the bottom of the row it is close, while the next
            // line of text is not, so the two stay apart. Left on its own it
            // becomes a row of its own and a table of note fields ends up with
            // twice the rows it has.
            double bottom = 0.0;
            for (const ContentItem &c : row) bottom = qMax(bottom, c.bounds.bottom());
            const bool rule = item.bounds.height() <= qMax(1.5, lineH * 0.3);
            const bool trails = rule && item.bounds.top() >= bottom
                             && item.bounds.top() - bottom <= lineH * 1.5;

            if (trails
                || qAbs(row.first().bounds.center().y() - item.bounds.center().y()) <= tol) {
                rows.last().append(item);
                continue;
            }
        }
        rows.append({ item });
    }
    for (QList<ContentItem> &row : rows)
        std::sort(row.begin(), row.end(),
                  [](const ContentItem &a, const ContentItem &b) {
            return a.bounds.left() < b.bounds.left();
        });
    return rows;
}

double medianHeight(const QList<ContentItem> &items)
{
    QList<double> h;
    for (const ContentItem &i : items) h.append(qMax(4.0, i.bounds.height()));
    return qMax(4.0, median(h));
}

QList<RawTable> seedTables(const QList<ContentItem> &cells)
{
    QList<RawTable> tables;
    for (const QList<ContentItem> &row : clusterRows(cells)) {
        QRectF rowBounds;
        for (const ContentItem &c : row) rowBounds = rowBounds.united(c.bounds);

        bool appended = false;
        if (!tables.isEmpty()) {
            RawTable &t = tables.last();
            const double lineH   = qMax(rowBounds.height(), 6.0);
            const double vGap    = rowBounds.top() - t.bounds.bottom();
            const double overlap = qMin(t.bounds.right(), rowBounds.right())
                                 - qMax(t.bounds.left(), rowBounds.left());
            if (vGap > -lineH && vGap < lineH * 3.2
                    && overlap > rowBounds.width() * 0.5) {
                t.items.append(row);
                t.bounds = t.bounds.united(rowBounds);
                appended = true;
            }
        }
        if (!appended) tables.append(RawTable{ row, rowBounds });
    }

    tables.erase(std::remove_if(tables.begin(), tables.end(), [](const RawTable &t) {
                     return clusterRows(t.items).size() < 2;
                 }), tables.end());
    return tables;
}

void absorbRows(RawTable &table, const QList<ContentItem> &pool,
                QList<bool> &taken)
{
    for (bool grew = true; grew; ) {
        grew = false;
        const double lineH = medianHeight(table.items);

        QList<ContentItem> free;
        QList<int>         freeIndex;
        for (int i = 0; i < pool.size(); ++i) {
            if (taken[i]) continue;
            const ContentItem &item = pool[i];
            const double covered = qMin(table.bounds.right(), item.bounds.right())
                                 - qMax(table.bounds.left(),  item.bounds.left());
            if (covered < item.bounds.width() * 0.6) continue;
            free.append(item);
            freeIndex.append(i);
        }
        if (free.isEmpty()) break;

        for (const QList<ContentItem> &row : clusterRows(free)) {
            QRectF rowBounds;
            for (const ContentItem &c : row) rowBounds = rowBounds.united(c.bounds);
            const double gap = qMax(0.0, qMax(rowBounds.top() - table.bounds.bottom(),
                                              table.bounds.top() - rowBounds.bottom()));

            // A single line right above or below the table can be the rest of a
            // cell that wrapped, but it can just as well be the paragraph the
            // table follows. A cell's leftover never grows much wider than the
            // widest cell already in the table, while a paragraph spans the
            // whole width, so that is what tells them apart. Without it the
            // table swallows the text above it and every column with it.
            double widest = 0.0;
            for (const ContentItem &item : table.items)
                widest = qMax(widest, item.bounds.width());

            const bool fullRow      = row.size() >= 2 && gap <= lineH * 2.8;
            const bool continuation = row.size() == 1 && gap <= lineH * 0.6
                                   && rowBounds.width() <= widest * 1.5;
            if (!fullRow && !continuation) continue;

            for (const ContentItem &c : row) {
                for (int k = 0; k < free.size(); ++k) {
                    if (taken[freeIndex[k]]) continue;
                    if (free[k].bounds != c.bounds || free[k].text != c.text) continue;
                    taken[freeIndex[k]] = true;
                    break;
                }
                table.items.append(c);
                table.bounds = table.bounds.united(c.bounds);
            }
            grew = true;
        }
    }
}

void refineTableExtent(const Raster &erased, QRgb paper, RawTable &table,
                       const QRectF &pageText)
{
    if (erased.isNull()) return;

    const double lineH = medianHeight(table.items);
    const QRect box = erased.toPx(table.bounds.adjusted(0.0, -lineH * 3.0,
                                                        0.0,  lineH * 0.5));
    if (box.isEmpty()) return;

    const int spanL = pageText.isNull() ? box.left()
        : qMax(0, qRound((pageText.left() - 6.0) * erased.scale()));
    const int spanR = pageText.isNull() ? box.right()
        : qMin(erased.width() - 1, qRound((pageText.right() + 6.0) * erased.scale()));
    if (spanR - spanL < 8) return;

    int bestLeft = 0, bestRight = 0, bestRun = 0;
    for (int y = box.top(); y <= box.bottom(); ++y) {
        int first = -1, last = -1, inked = 0;
        for (int x = spanL; x <= spanR; ++x) {
            if (colorDist(erased.at(x, y), paper) <= 24) continue;
            if (first < 0) first = x;
            last = x;
            ++inked;
        }
        if (first < 0 || inked * 10 < (spanR - spanL) * 6) continue;
        if (last - first > bestRun) { bestRun = last - first; bestLeft = first; bestRight = last; }
    }
    if (bestRun <= box.width() / 2) return;

    const QRectF found = erased.toPt(QRect(QPoint(bestLeft, box.top()),
                                           QPoint(bestRight, box.bottom())));

    const double limitL = pageText.isNull() ? found.left()
                                            : qMax(found.left(), pageText.left());
    const double limitR = pageText.isNull() ? found.right()
                                            : qMin(found.right(), pageText.right());
    table.bounds.setLeft (qMin(table.bounds.left(),  limitL));
    table.bounds.setRight(qMax(table.bounds.right(), limitR));
    table.measured = true;
}

/// A drawn rule: where it runs, how thick and in which colour.
struct Rule {
    double x       { 0.0 };
    double widthPt { 0.0 };
    QColor color;
};

/// Thin rules drawn through at least half of an area, across it (vertical) or
/// along it (horizontal), in order. Shading wider than a rule does not count.
/// A thin rule is spread over partly covered pixels, so a grey one is taken
/// for a narrower black one, which looks the same.
QList<Rule> findRules(const Raster &erased, QRgb paper, const QRectF &area, bool vertical)
{
    QList<Rule> found;
    const QRect box = erased.toPx(area);
    if (erased.isNull() || box.width() < 4 || box.height() < 4) return found;
    const int widest = qMax(2, qRound(3.0 * erased.scale()));
    const int from = vertical ? box.left() : box.top();
    const int to   = vertical ? box.right() : box.bottom();
    const int span = vertical ? box.height() : box.width();
    const auto at = [&](int along, int across) {
        return vertical ? erased.at(along, across) : erased.at(across, along);
    };
    const int acrossFrom = vertical ? box.top() : box.left();
    int start = -1;
    for (int k = from; k <= to + 1; ++k) {
        bool inked = false;
        if (k <= to) {
            int hits = 0;
            for (int a = acrossFrom; a < acrossFrom + span; ++a)
                if (colorDist(at(k, a), paper) > 24) ++hits;
            inked = hits * 2 >= span;
        }
        if (inked && start < 0) start = k;
        if (!inked && start >= 0) {
            if (k - start <= widest) {
                QRgb darkest = paper;
                const int middle = acrossFrom + span / 2;
                for (int a = middle - 2; a <= middle + 2; ++a)
                    for (int px = start; px < k; ++px)
                        if (colorDist(at(px, a), paper) > colorDist(darkest, paper))
                            darkest = at(px, a);
                const QColor dark = QColor::fromRgb(darkest);
                const bool neutral = qAbs(dark.red() - dark.green()) < 16
                                  && qAbs(dark.green() - dark.blue()) < 16;
                const QRgb ink = neutral ? qRgb(0, 0, 0) : darkest;
                double cover = 0.0;
                for (int a = middle - 2; a <= middle + 2; ++a)
                    for (int px = start; px < k; ++px)
                        cover += qMin(1.0, double(colorDist(at(px, a), paper))
                                               / qMax(1, colorDist(ink, paper)));
                found.append({ (start + k) / 2.0 / erased.scale(),
                               qMax(0.25, qRound(cover / 5.0 / erased.scale() * 4.0) / 4.0),
                               QColor::fromRgb(ink) });
            }
            start = -1;
        }
    }
    return found;
}

DocxBlock buildTableBlock(const RawTable &raw, const Raster &erased, QRgb paper)
{
    QList<QList<ContentItem>> rows = clusterRows(raw.items);
    if (rows.size() < 2) return {};

    QList<double> sizes;
    for (const ContentItem &c : raw.items) sizes.append(fontSizeOf(c));
    const double fs = qMax(6.0, median(sizes));

    // An underline that trails a row is not a line of text: counted as one it
    // makes the row look fuller and reach further down, and the row below then
    // looks like a wrapped remainder and gets merged into it.
    const auto isRule = [fs](const ContentItem &c) {
        return c.bounds.height() <= qMax(1.5, fs * 0.3);
    };
    const auto textCount = [&isRule](const QList<ContentItem> &row) {
        int n = 0;
        for (const ContentItem &c : row) if (!isRule(c)) ++n;
        return n;
    };

    for (int r = rows.size() - 1; r >= 1; --r) {
        QRectF above, here;
        for (const ContentItem &c : rows[r - 1])
            if (!isRule(c)) above = above.united(c.bounds);
        for (const ContentItem &c : rows[r])
            if (!isRule(c)) here = here.united(c.bounds);
        if (above.isNull() || here.isNull()) continue;
        if (textCount(rows[r]) >= textCount(rows[r - 1])) continue;
        if (here.top() - above.bottom() > fs * 0.6) continue;
        rows[r - 1].append(rows[r]);
        std::sort(rows[r - 1].begin(), rows[r - 1].end(),
                  [](const ContentItem &a, const ContentItem &b) {
            return a.bounds.left() < b.bounds.left();
        });
        rows.removeAt(r);
    }

    const double x0 = raw.bounds.left()  - fs;
    const double x1 = raw.bounds.right() + fs;
    const int    bins = qMax(4, qCeil(x1 - x0));
    QVector<int> occupancy(bins, 0);
    for (const QList<ContentItem> &row : rows) {
        QVector<bool> hit(bins, false);
        for (const ContentItem &c : row) {
            const int a = qBound(0, qFloor(c.bounds.left()  - x0), bins - 1);
            const int b = qBound(0, qCeil (c.bounds.right() - x0), bins - 1);
            for (int i = a; i <= b; ++i) hit[i] = true;
        }
        for (int i = 0; i < bins; ++i) if (hit[i]) ++occupancy[i];
    }

    const int tolerated = rows.size() >= 8 ? rows.size() / 6 : 0;

    QList<double> edges;
    edges.append(raw.measured ? raw.bounds.left() : x0 + fs * 0.5);
    for (int i = 1; i < bins; ) {
        if (occupancy[i] > tolerated) { ++i; continue; }
        int j = i;
        while (j < bins && occupancy[j] <= tolerated) ++j;
        const double gapPt = j - i;

        if (gapPt >= fs * 0.9 && j < bins && occupancy[i - 1] > tolerated)
            edges.append(x0 + (i + j) / 2.0);
        i = j;
    }
    edges.append(qMax(raw.measured ? raw.bounds.right() : x1 - fs * 0.5,
                      edges.last() + fs));

    // A table that draws its grid says where its columns are; the gaps between
    // the words only guess, and split a column whose heading and numbers are
    // aligned to opposite sides.
    QRectF textSpan;
    for (const QList<ContentItem> &row : rows)
        for (const ContentItem &c : row) textSpan = textSpan.united(c.bounds);
    const QList<Rule> rules = findRules(
        erased, paper, QRectF(x0 - fs, textSpan.top(), x1 - x0 + 2.0 * fs, textSpan.height()), true);
    QList<double> inner, innerWidths;
    for (const Rule &rule : rules)
        if (rule.x > textSpan.left() + 1.0 && rule.x < textSpan.right() - 1.0) {
            inner.append(rule.x);
            innerWidths.append(rule.widthPt);
        }
    const bool outerLeft  = !rules.isEmpty() && rules.first().x <= textSpan.left() + 1.0;
    const bool outerRight = !rules.isEmpty() && rules.last().x >= textSpan.right() - 1.0;
    if (!inner.isEmpty()) {
        const double left  = outerLeft  ? rules.first().x : edges.first();
        const double right = outerRight ? rules.last().x  : edges.last();
        edges = { left };
        edges += inner;
        edges.append(right);
    }
    const int cols = edges.size() - 1;
    if (cols < 1) return {};

    const auto columnOf = [&](const ContentItem &cell) {
        const double cx = cell.bounds.center().x();
        for (int c = 0; c < cols; ++c)
            if (cx >= edges[c] && cx < edges[c + 1]) return c;
        return cx < edges.first() ? 0 : cols - 1;
    };

    QList<QRectF> rowBounds;
    for (const QList<ContentItem> &row : rows) {
        QRectF b;
        for (const ContentItem &c : row) b = b.united(c.bounds);
        rowBounds.append(b);
    }
    QList<double> rowEdges;
    rowEdges.append(qMin(raw.bounds.top(), rowBounds.first().top() - fs * 0.4));
    for (int r = 1; r < rows.size(); ++r)
        rowEdges.append((rowBounds[r - 1].bottom() + rowBounds[r].top()) / 2.0);
    rowEdges.append(qMax(raw.bounds.bottom(),
                         rowBounds.last().bottom() + fs * 0.4));

    // Drawn rules between the rows are where the rows really end.
    const QList<Rule> across = findRules(
        erased, paper, QRectF(edges.first(), rowEdges.first() - fs * 2.5,
                              edges.last() - edges.first(),
                              rowEdges.last() - rowEdges.first() + fs * 5.0), false);
    const auto ruleNear = [&across](double from, double to, double aim) {
        double best = -1.0;
        for (const Rule &rule : across)
            if (rule.x >= from && rule.x <= to
                    && (best < 0.0 || qAbs(rule.x - aim) < qAbs(best - aim)))
                best = rule.x;
        return best;
    };
    if (!across.isEmpty()) {
        if (const double top = ruleNear(rowBounds.first().top() - fs * 2.5,
                                        rowBounds.first().top(), rowBounds.first().top());
                top >= 0.0)
            rowEdges.first() = top;
        for (int r = 1; r < rows.size(); ++r)
            if (const double y = ruleNear(rowBounds[r - 1].bottom() - 1.0, rowBounds[r].top() + 1.0,
                                          rowEdges[r]);
                    y >= 0.0)
                rowEdges[r] = y;
        if (const double bottom = ruleNear(rowBounds.last().bottom(),
                                           rowBounds.last().bottom() + fs * 2.5,
                                           rowBounds.last().bottom());
                bottom >= 0.0)
            rowEdges.last() = bottom;
    }

    DocxBlock block;
    block.kind   = DocxBlock::Kind::Table;
    block.bounds = QRectF(edges.first(), rowEdges.first(),
                          edges.last() - edges.first(),
                          rowEdges.last() - rowEdges.first());
    block.table.rowCount = rows.size();
    // A drawn grid keeps its colour and the weight of its frame and its inside.
    if (!inner.isEmpty()) {
        block.table.ruleColor   = rules[rules.size() / 2].color;
        block.table.ruleInnerPt = median(innerWidths);
        block.table.ruleOuterPt = outerLeft ? rules.first().widthPt : block.table.ruleInnerPt;
    }
    for (int c = 0; c < cols; ++c)
        block.table.colWidthsPt.append(qMax(8.0, edges[c + 1] - edges[c]));
    for (int r = 0; r < rows.size(); ++r)
        block.table.rowHeightsPt.append(qMax(6.0, rowEdges[r + 1] - rowEdges[r]));

    for (int r = 0; r < rows.size(); ++r) {
        QSet<int> filled;
        QSet<int> occupied;
        const auto rowColumn = [&](int index) {

            return rows[r].size() == cols ? index : columnOf(rows[r][index]);
        };
        for (int i = 0; i < rows[r].size(); ++i)
            occupied.insert(rowColumn(i));
        for (int i = 0; i < rows[r].size(); ++i) {
            const ContentItem &cell = rows[r][i];
            DocxCell dc;
            dc.item = cell;
            dc.row  = r;
            dc.col  = rowColumn(i);

            if (filled.contains(dc.col)) {
                for (DocxCell &existing : block.table.cells)
                    if (existing.row == r && existing.col == dc.col) {
                        existing.item.text += u' ' + cell.text;
                        break;
                    }
                continue;
            }
            filled.insert(dc.col);

            for (int c = dc.col + 1; c < cols; ++c) {
                if (occupied.contains(c)) break;
                const double covered = qMin(cell.bounds.right(), edges[c + 1])
                                     - qMax(cell.bounds.left(), edges[c]);
                if (covered < (edges[c + 1] - edges[c]) * 0.4) break;
                ++dc.colSpan;
            }
            const int endCol = qMin(cols, dc.col + dc.colSpan);
            const QRectF cellArea(edges[dc.col], rowEdges[r],
                                  edges[endCol] - edges[dc.col],
                                  rowEdges[r + 1] - rowEdges[r]);

            dc.shading = cell.bgColor.isValid()
                ? cell.bgColor : dominantFill(erased, paper, cellArea);
            block.table.cells.append(dc);
        }
    }

    for (int c = 0; c < cols; ++c) {
        QList<DocxCell *> body, header;
        for (DocxCell &cell : block.table.cells) {
            if (cell.col != c || cell.colSpan != 1) continue;
            (cell.row == 0 ? header : body).append(&cell);
        }
        enum class Edge { Left, Centre, Right };
        const auto spread = [](const QList<DocxCell *> &group, Edge edge) {
            double lo = 1e18, hi = -1e18;
            for (const DocxCell *cell : group) {
                const QRectF &r = cell->item.bounds;
                const double v = edge == Edge::Left   ? r.left()
                               : edge == Edge::Centre ? r.center().x() : r.right();
                lo = qMin(lo, v);
                hi = qMax(hi, v);
            }
            return hi - lo;
        };
        bool rightAligned = false;
        if (body.size() >= 2) {
            const double left   = spread(body, Edge::Left);
            const double centre = spread(body, Edge::Centre);
            const double right  = spread(body, Edge::Right);
            rightAligned = left >= fs * 0.3 && right < left * 0.5 && right <= centre;
            const Qt::Alignment align = rightAligned ? Qt::AlignRight
                : centre + fs * 0.5 < left ? Qt::AlignHCenter : Qt::AlignLeft;
            for (DocxCell *cell : body) cell->align = align;
        }

        if (body.isEmpty()) continue;
        double bodyLeft = 1e18, bodyRight = -1e18;
        for (const DocxCell *b : body) {
            bodyLeft  = qMin(bodyLeft,  b->item.bounds.left());
            bodyRight = qMax(bodyRight, b->item.bounds.right());
        }

        for (DocxCell *cell : header) {
            if (rightAligned && qAbs(cell->item.bounds.right() - bodyRight) <= fs * 0.5)
                cell->align = Qt::AlignRight;
            else if (cell->item.bounds.left() - bodyLeft >= fs * 1.2)
                cell->align = Qt::AlignHCenter;
        }
    }

    // The room a cell keeps to its rules, measured where text is set against them.
    QList<double> lefts, rights;
    for (const DocxCell &cell : std::as_const(block.table.cells)) {
        const int end = qMin(cols, cell.col + cell.colSpan);
        if (cell.align & Qt::AlignRight)
            rights.append(edges[end] - cell.item.bounds.right());
        else if (!(cell.align & Qt::AlignHCenter))
            lefts.append(cell.item.bounds.left() - edges[cell.col]);
    }
    if (!lefts.isEmpty())  block.table.padLeftPt  = qBound(0.0, median(lefts), 12.0);
    if (!rights.isEmpty()) block.table.padRightPt = qBound(0.0, median(rights), 12.0);
    return block;
}

/// Whether the rendered page shows more at an image's place than the image
/// itself: text that was erased there counts little, a panel drawn over it a lot.
bool overdrawn(const QImage &own, const QImage &seen, QRgb paper)
{
    if (own.isNull() || seen.isNull()) return true;
    QImage flat(seen.size(), QImage::Format_RGB32);
    flat.fill(paper);
    QPainter painter(&flat);
    painter.drawImage(flat.rect(), own);
    painter.end();
    const QImage page = seen.convertToFormat(QImage::Format_RGB32);
    qint64 differing = 0, total = 0;
    for (int y = 0; y < flat.height(); y += 2)
        for (int x = 0; x < flat.width(); x += 2) {
            ++total;
            if (colorDist(flat.pixel(x, y), page.pixel(x, y)) > 60) ++differing;
        }
    return total == 0 || differing * 20 > total;
}

/// The item's text as runs, so that more can be appended in another look.
QList<ContentRun> runsOf(const ContentItem &item)
{
    if (!item.runs.isEmpty()) return item.runs;
    return { { 0, int(item.text.size()), item.fontFamily, item.bold, item.italic,
               item.fontSizePt, item.textColor, item.textOrigin.x(), false } };
}

/// Lines that stand side by side on one baseline, a label and its value or a
/// heading and its date, share one line: the later ones are reached with a tab
/// at the place they started. Apart they would be stacked below each other.
void joinSideBySide(QList<DocxBlock> &paragraphs)
{
    std::sort(paragraphs.begin(), paragraphs.end(), [](const DocxBlock &a, const DocxBlock &b) {
        return a.bounds.left() < b.bounds.left();
    });
    for (int i = 0; i < paragraphs.size(); ++i) {
        DocxBlock &left = paragraphs[i];
        if (left.lines.size() != 1 || left.lines.first().textOrigin.y() <= 0.0) continue;
        for (int j = i + 1; j < paragraphs.size();) {
            const DocxBlock &right = paragraphs[j];
            ContentItem &line = left.lines.first();
            const ContentItem &next = right.lines.first();
            if (right.lines.size() != 1 || next.textOrigin.y() <= 0.0
                    || qAbs(next.textOrigin.y() - line.textOrigin.y()) > 1.0
                    || next.bounds.left() <= line.bounds.right()) {
                ++j;
                continue;
            }
            QList<ContentRun> runs = runsOf(line);
            const int offset = int(line.text.size()) + 1;
            QList<ContentRun> added = runsOf(next);
            added.first().x     = next.textOrigin.x() > 0.0 ? next.textOrigin.x()
                                                            : next.bounds.left();
            added.first().apart = true;
            for (ContentRun &run : added) {
                run.start += offset;
                runs.append(run);
            }
            line.text  += u' ' + next.text;
            line.runs   = runs;
            line.bounds = line.bounds.united(next.bounds);
            left.bounds = left.bounds.united(right.bounds);
            paragraphs.removeAt(j);
        }
    }
}

QList<DocxBlock> buildParagraphs(const QList<ContentItem> &lines)
{
    QList<ContentItem> sorted = lines;
    std::sort(sorted.begin(), sorted.end(),
              [](const ContentItem &a, const ContentItem &b) {
        if (qAbs(a.bounds.top() - b.bounds.top()) > 1.0)
            return a.bounds.top() < b.bounds.top();
        return a.bounds.left() < b.bounds.left();
    });

    // A line that opens with a bullet starts its own paragraph, whatever the
    // spacing says. Without this a whole list reads as one run-on block.
    const auto startsListItem = [](const ContentItem &line) {
        const QString text = line.text.trimmed();
        if (text.isEmpty()) return false;
        const QChar first = text.at(0);
        static const QString bullets =
            QStringLiteral("\u2022\u2023\u2043\u2219\u25AA\u25CF\u25E6\u00B7");
        if (bullets.contains(first)) return true;
        // Symbol and Wingdings keep their bullets in the private use area.
        const ushort code = first.unicode();
        return code >= 0xF000 && code <= 0xF0FF;
    };

    QList<DocxBlock> blocks;
    for (const ContentItem &line : sorted) {
        bool merged = false;
        if (!blocks.isEmpty()) {
            DocxBlock &prev = blocks.last();
            const ContentItem &tail = prev.lines.last();
            const double fs   = qMax(fontSizeOf(tail), fontSizeOf(line));
            const double vGap = line.bounds.top() - tail.bounds.bottom();
            const bool   flows = vGap > -fs * 0.5 && vGap < fs * 0.75;
            const bool   leftAligned =
                qAbs(tail.bounds.left() - line.bounds.left()) < fs * 0.5;
            const bool   centred =
                qAbs(tail.bounds.center().x() - line.bounds.center().x()) < fs * 0.8
                && qAbs(tail.bounds.left() - line.bounds.left()) > fs * 0.5;
            const double xOverlap = qMin(tail.bounds.right(), line.bounds.right())
                                  - qMax(tail.bounds.left(),  line.bounds.left());
            if (flows && (leftAligned || centred) && xOverlap > 1.0
                    && sameTextStyle(tail, line) && !startsListItem(line)) {
                prev.lines.append(line);
                prev.bounds = prev.bounds.united(line.bounds);
                merged = true;
            }
        }
        if (!merged) {
            DocxBlock b;
            b.kind   = DocxBlock::Kind::Paragraph;
            b.lines  = { line };
            b.bounds = line.bounds;
            blocks.append(b);
        }
    }
    return blocks;
}

struct Region {
    QRect  px;
    int    inkCells { 0 };
};

QList<Region> findGraphicRegions(const Raster &erased, QRgb paper,
                                 const QList<QRectF> &claimed)
{
    QList<Region> regions;
    if (erased.isNull()) return regions;

    const int cell = qMax(1, qRound(2.0 * erased.scale()));
    const int gw = (erased.width()  + cell - 1) / cell;
    const int gh = (erased.height() + cell - 1) / cell;
    if (gw <= 0 || gh <= 0) return regions;

    QVector<quint8> ink(size_t(gw) * gh, 0);
    for (int gy = 0; gy < gh; ++gy) {
        for (int gx = 0; gx < gw; ++gx) {
            bool found = false;
            for (int y = gy * cell; y < (gy + 1) * cell && !found; ++y)
                for (int x = gx * cell; x < (gx + 1) * cell && !found; ++x)
                    if (colorDist(erased.at(x, y), paper) > 26) found = true;
            ink[size_t(gy) * gw + gx] = found ? 1 : 0;
        }
    }

    for (const QRectF &rect : claimed) {
        const QRect px = erased.toPx(rect);
        for (int gy = px.top() / cell; gy <= px.bottom() / cell; ++gy)
            for (int gx = px.left() / cell; gx <= px.right() / cell; ++gx)
                if (gx >= 0 && gy >= 0 && gx < gw && gy < gh)
                    ink[size_t(gy) * gw + gx] = 0;
    }

    QVector<quint8> seen(size_t(gw) * gh, 0);
    for (int gy = 0; gy < gh; ++gy) {
        for (int gx = 0; gx < gw; ++gx) {
            const size_t start = size_t(gy) * gw + gx;
            if (!ink[start] || seen[start]) continue;
            QStack<QPoint> stack;
            stack.push(QPoint(gx, gy));
            seen[start] = 1;
            Region region;
            region.px = QRect(gx * cell, gy * cell, cell, cell);
            while (!stack.isEmpty()) {
                const QPoint p = stack.pop();
                ++region.inkCells;
                region.px = region.px.united(QRect(p.x() * cell, p.y() * cell,
                                                   cell, cell));

                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) {
                        const int nx = p.x() + dx, ny = p.y() + dy;
                        if (nx < 0 || ny < 0 || nx >= gw || ny >= gh) continue;
                        const size_t n = size_t(ny) * gw + nx;
                        if (ink[n] && !seen[n]) { seen[n] = 1; stack.push(QPoint(nx, ny)); }
                    }
            }
            regions.append(region);
        }
    }

    regions.erase(std::remove_if(regions.begin(), regions.end(),
                                 [&](const Region &r) {
                                     const double w = r.px.width()  / erased.scale();
                                     const double h = r.px.height() / erased.scale();
                                     return r.inkCells < 2 || w * h < 36.0;
                                 }),
                  regions.end());
    return regions;
}

bool isFlatFill(const Raster &erased, QRgb paper, const QRect &box,
                const QList<QRectF> &claimed, QRgb *fillOut)
{
    if (box.width() < 4 || box.height() < 4) return false;

    QHash<QRgb, int> counts;
    const int step = qMax(1, qMin(box.width(), box.height()) / 40);
    for (int y = box.top(); y <= box.bottom(); y += step)
        for (int x = box.left(); x <= box.right(); x += step)
            ++counts[erased.at(x, y)];
    QRgb fill = paper;
    int  bestN = 0, total = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        total += it.value();
        if (it.value() > bestN) { bestN = it.value(); fill = it.key(); }
    }
    if (total == 0 || colorDist(fill, paper) < 12) return false;
    if (double(bestN) / total < 0.55) return false;

    const int band = qMax(2, qRound(2.5 * erased.scale()));
    const QRect inner = box.adjusted(band, band, -band, -band);
    int strayInterior = 0, interior = 0;
    for (int y = box.top(); y <= box.bottom(); y += step) {
        for (int x = box.left(); x <= box.right(); x += step) {
            const QRgb c = erased.at(x, y);
            if (colorDist(c, fill) <= 40) continue;
            if (!inner.contains(x, y)) continue;
            bool inText = false;
            for (const QRectF &r : claimed)
                if (erased.toPx(r).adjusted(-2, -2, 2, 2).contains(x, y)) { inText = true; break; }
            if (inText) continue;
            ++strayInterior;
        }
        interior += (box.width() / qMax(1, step));
    }
    if (interior > 0 && double(strayInterior) / interior > 0.012) return false;

    if (fillOut) *fillOut = fill;
    return true;
}

}

QList<DocxBlock> buildDocxBlocks(const DocxLayoutInput &in, QMarginsF *marginsOut)
{
    QList<ContentItem> text, cells;
    QList<DocxBlock> fields;
    for (const ContentItem &item : in.items) {
        if (!item.isTextual() || item.text.trimmed().isEmpty()) continue;
        if (item.type == ContentItem::Type::FormField) {
            // A field's value sits at the field, beside its label, not in the
            // flow below it; the field shows it centred on its height.
            const double fs = fontSizeOf(item);
            DocxBlock value;
            value.kind   = DocxBlock::Kind::TextBox;
            value.bounds = QRectF(item.bounds.left() + 2.0, item.bounds.center().y() - fs * 0.6,
                                  qMax(1.0, item.bounds.width() - 4.0), fs * 1.2);
            value.lines  = { item };
            fields.append(value);
        } else if (item.type == ContentItem::Type::TableCell) {
            cells.append(item);
        } else {
            text.append(item);
        }
    }
    if (text.isEmpty() && cells.isEmpty() && fields.isEmpty()) return {};

    if (layoutDebug()) {
        std::fprintf(stderr, "── items\n");
        for (const ContentItem &it : in.items) {
            if (!it.isTextual() || it.text.trimmed().isEmpty()) continue;
            std::fprintf(stderr, "  %-9s %6.1f,%6.1f %6.1fx%-5.1f fs=%4.1f | %s\n",
                         it.type == ContentItem::Type::TableCell ? "CELL"
                         : it.type == ContentItem::Type::Paragraph ? "PARA" : "TEXT",
                         it.bounds.left(), it.bounds.top(),
                         it.bounds.width(), it.bounds.height(),
                         it.fontSizePt, qUtf8Printable(it.text.left(40)));
        }
    }

    const Raster erased(in.erased, in.scale);
    const QRgb   paper = paperColor(erased);

    QRectF pageText;
    for (const ContentItem &i : text)  pageText = pageText.united(i.bounds);
    for (const ContentItem &i : cells) pageText = pageText.united(i.bounds);

    QList<RawTable> raw = seedTables(cells);

    for (RawTable &t : raw) refineTableExtent(erased, paper, t, pageText);

    QList<bool> taken(text.size(), false);
    for (RawTable &t : raw) absorbRows(t, text, taken);

    QList<QRectF>          claimed;
    QList<QRectF>          tableAreas;
    QList<const RawTable *> tableSources;
    QList<DocxBlock>       blocks;
    for (RawTable &t : raw) {
        DocxBlock block = buildTableBlock(t, erased, paper);
        if (block.table.colWidthsPt.isEmpty()) continue;

        blocks.append(block);
        tableAreas.append(block.bounds.united(t.bounds));
        tableSources.append(&t);
    }

    for (const ContentItem &c : cells) {
        bool inTable = false;
        for (const QRectF &area : tableAreas)
            if (area.contains(c.bounds.center())) { inTable = true; break; }
        if (!inTable) text.append(c);
    }

    {
        QList<ContentItem> rest;
        for (int i = 0; i < text.size(); ++i)
            if (i >= taken.size() || !taken[i]) rest.append(text[i]);
        text = rest;
    }
    for (const ContentItem &t : text)
        claimed.append(t.bounds.adjusted(-1.0, -1.0, 1.0, 1.0));

    QSet<int>  bakedText;
    QSet<int>  droppedTables;
    QSet<int>  overlaidTables;
    QList<DocxBlock> pictures;
    // Text over a picture goes into a box of its own on top of it.
    const auto addLabels = [&](const QRectF &area) {
        for (int i = 0; i < text.size(); ++i) {
            if (bakedText.contains(i) || !area.contains(text[i].bounds.center())) continue;
            DocxBlock label;
            label.kind   = DocxBlock::Kind::TextBox;
            label.bounds = text[i].bounds;
            label.lines  = { text[i] };
            pictures.append(label);
            bakedText.insert(i);
        }
    };
    const auto area = [](const QRectF &r) { return r.width() * r.height(); };

    // Every image object stays a picture of its own at its own place, also one
    // too pale to stand out from the paper, in its own pixels where it shows
    // uncut and as the rendered page where it is cropped or turned. They go on
    // top of what the rendered page gives around them.
    QList<DocxBlock> imagePictures;
    QList<QRectF> keptImages;
    for (const DocxSourceImage &source : in.images) {
        const QRectF box = source.bounds.intersected(QRectF(QPointF(0.0, 0.0), in.pageSizePt));
        if (area(box) < 4.0) continue;
        const bool uncut = !source.image.isNull()
            && qAbs(double(source.image.width()) / source.image.height()
                    - box.width() / box.height()) < box.width() / box.height() * 0.03;
        // Something drawn over the image, a panel or a frame, is only on the
        // rendered page; there the region takes the image with all on top of it.
        if (uncut && overdrawn(source.image, erased.crop(erased.toPx(box)), paper)) continue;
        keptImages.append(box);
        DocxBlock pic;
        pic.kind    = DocxBlock::Kind::Picture;
        pic.bounds  = box;
        pic.picture = DocxXml::encodePicture(uncut ? source.image : erased.crop(erased.toPx(box)),
                                             in.options);
        if (pic.picture.isNull()) continue;
        imagePictures.append(pic);
        addLabels(box);
    }

    for (const Region &region : findGraphicRegions(erased, paper, claimed)) {
        QRgb fill = paper;
        const QRectF boundsPt = erased.toPt(region.px);
        const double regionArea = boundsPt.width() * boundsPt.height();

        bool isTableDecoration = false;
        for (int t = 0; t < tableAreas.size(); ++t) {
            const QRectF hit = tableAreas[t].intersected(boundsPt);
            const double share = hit.width() * hit.height();
            const double tableArea = tableAreas[t].width() * tableAreas[t].height();
            if (tableArea <= 0.0 || share <= 0.0) continue;

            if (share >= regionArea * 0.8 && regionArea <= tableArea * 1.6) {
                isTableDecoration = true;
                break;
            }

            if (share >= tableArea * 0.8 && regionArea > tableArea * 1.15) {
                droppedTables.insert(t);
                if (!overlaidTables.contains(t) && t < blocks.size()) {
                    overlaidTables.insert(t);
                    // The pieces the table was built from, not the cells it was
                    // built into: where the columns could not be told apart a
                    // whole row ends up in one cell, and written into the box of
                    // its first column the rest of the row reads as empty. The
                    // raw pieces also still hold the heading and the header row,
                    // which were absorbed into the table and exist nowhere else
                    // by now. All of them are kept, not only the ones the
                    // picture covers.
                    for (const ContentItem &piece : tableSources[t]->items) {
                        DocxBlock label;
                        label.kind   = DocxBlock::Kind::TextBox;
                        label.bounds = piece.bounds;
                        label.lines  = { piece };
                        pictures.append(label);
                    }
                }
            }
        }
        if (isTableDecoration) continue;

        // Image objects are pictures of their own already; of a region they
        // cover only what lies outside them comes from the rendered page.
        double covered = 0.0;
        for (const QRectF &kept : std::as_const(keptImages))
            covered += area(kept.intersected(boundsPt));
        if (covered >= regionArea * 0.85) continue;
        if (covered > 0.0) {
            QImage rest = erased.crop(region.px).convertToFormat(QImage::Format_ARGB32);
            QPainter blank(&rest);
            blank.setCompositionMode(QPainter::CompositionMode_Source);
            for (const QRectF &kept : std::as_const(keptImages)) {
                const QRect px = erased.toPx(kept).intersected(region.px);
                if (!px.isEmpty()) blank.fillRect(px.translated(-region.px.topLeft()), Qt::transparent);
            }
            blank.end();
            DocxBlock pic;
            pic.kind    = DocxBlock::Kind::Picture;
            pic.bounds  = boundsPt;
            pic.picture = DocxXml::encodePicture(rest, in.options);
            if (!pic.picture.isNull()) pictures.append(pic);
            addLabels(boundsPt);
            continue;
        }

        const bool thinRule = boundsPt.height() <= 3.5 || boundsPt.width() <= 3.5;
        const QColor ruleFill = thinRule ? dominantInkColor(erased, paper, boundsPt)
                                         : QColor{};
        if ((thinRule && ruleFill.isValid())
                || isFlatFill(erased, paper, region.px, claimed, &fill)) {

            DocxBlock shape;
            shape.kind      = DocxBlock::Kind::Shape;
            shape.bounds    = boundsPt;
            shape.fillColor = ruleFill.isValid() ? ruleFill : QColor::fromRgb(fill);
            pictures.append(shape);
            continue;
        }
        DocxBlock pic;
        pic.kind    = DocxBlock::Kind::Picture;
        pic.bounds  = boundsPt;

        pic.picture = DocxXml::encodePicture(erased.crop(region.px), in.options);
        if (pic.picture.isNull()) continue;
        pictures.append(pic);
        addLabels(boundsPt);
    }
    pictures += imagePictures;

    for (int t = tableAreas.size() - 1; t >= 0; --t)
        if (droppedTables.contains(t) && t < blocks.size()) blocks.removeAt(t);
    blocks.append(pictures);

    QList<ContentItem> flowing;
    for (int i = 0; i < text.size(); ++i)
        if (!bakedText.contains(i)) flowing.append(text[i]);

    QList<DocxBlock> paragraphs = buildParagraphs(flowing);
    joinSideBySide(paragraphs);
    blocks.append(paragraphs);
    blocks.append(fields);

    std::sort(blocks.begin(), blocks.end(),
              [](const DocxBlock &a, const DocxBlock &b) {
        if (qAbs(a.bounds.top() - b.bounds.top()) > 1.0)
            return a.bounds.top() < b.bounds.top();
        return a.bounds.left() < b.bounds.left();
    });

    QRectF content;
    for (const DocxBlock &b : blocks)
        if (b.kind != DocxBlock::Kind::Picture
                && b.kind != DocxBlock::Kind::Shape
                && b.kind != DocxBlock::Kind::TextBox)
            content = content.united(b.bounds.adjusted(
                0.0, qMin(0.0, DocxXml::flowTop(b) - b.bounds.top()), 0.0, 0.0));
    if (content.isNull())
        for (const DocxBlock &b : blocks) content = content.united(b.bounds);

    QMarginsF margins(qBound(12.0, content.left(), in.pageSizePt.width() / 3.0),
                      qBound(12.0, content.top(),  in.pageSizePt.height() / 3.0),
                      qBound(12.0, in.pageSizePt.width() - content.right(),
                             in.pageSizePt.width() / 3.0),
                      12.0);
    // Line extents are measured on the ink, which is a little narrower than the
    // advance widths a word processor wraps by. Flowing text gets that much more
    // room on the right, or its last word drops to the next line.
    double largestFont = 0.0;
    for (const DocxBlock &b : blocks)
        if (b.kind == DocxBlock::Kind::Paragraph)
            for (const ContentItem &line : b.lines)
                largestFont = qMax(largestFont, line.fontSizePt);
    double wrapSlack = qBound(2.0, largestFont * 0.3, 5.0);
    // Full-width characters keep their ink to one side of their box, so a line
    // ending in one, like "。", runs up to a whole size past its ink.
    for (const DocxBlock &b : blocks)
        if (b.kind == DocxBlock::Kind::Paragraph)
            for (const ContentItem &line : b.lines) {
                const QString t = line.text.trimmed();
                if (!t.isEmpty() && t.back().unicode() >= 0x2E80)
                    wrapSlack = qMax(wrapSlack, qMin(line.fontSizePt, 24.0));
            }
    if (marginsOut)
        *marginsOut = QMarginsF(margins.left(), margins.top(),
                                qMax(0.0, margins.right() - wrapSlack), margins.bottom());

    if (layoutDebug()) {
        std::fprintf(stderr, "── page blocks (margins l=%.0f t=%.0f r=%.0f b=%.0f)\n",
              margins.left(), margins.top(), margins.right(), margins.bottom());
        for (const DocxBlock &b : blocks) {
            const QRectF &r = b.bounds;
            switch (b.kind) {
            case DocxBlock::Kind::Table:
                std::fprintf(stderr, "  TABLE   %6.1f,%6.1f %6.1fx%-6.1f cols=%lld rows=%d cells=%lld\n",
                      r.left(), r.top(), r.width(), r.height(),
                      b.table.colWidthsPt.size(), b.table.rowCount,
                      b.table.cells.size());
                break;
            case DocxBlock::Kind::Picture:
                std::fprintf(stderr, "  PICTURE %6.1f,%6.1f %6.1fx%-6.1f\n",
                      r.left(), r.top(), r.width(), r.height());
                break;
            case DocxBlock::Kind::Shape:
                std::fprintf(stderr, "  SHAPE   %6.1f,%6.1f %6.1fx%-6.1f fill=%s\n",
                      r.left(), r.top(), r.width(), r.height(),
                      qUtf8Printable(b.fillColor.name().mid(1)));
                break;
            case DocxBlock::Kind::TextBox:
                std::fprintf(stderr, "  TEXTBOX %6.1f,%6.1f %6.1fx%-6.1f | %s\n",
                      r.left(), r.top(), r.width(), r.height(),
                      qUtf8Printable(b.lines.first().text.left(46)));
                break;
            case DocxBlock::Kind::Paragraph:
                std::fprintf(stderr, "  PARA    %6.1f,%6.1f %6.1fx%-6.1f n=%lld | %s\n",
                      r.left(), r.top(), r.width(), r.height(), b.lines.size(),
                      qUtf8Printable(b.lines.first().text.left(46)));
                break;
            }
        }
    }

    const double textLeft  = margins.left();
    const double textRight = in.pageSizePt.width() - margins.right();
    for (DocxBlock &b : blocks) {
        if (b.kind == DocxBlock::Kind::Table) {
            b.table.indentPt = qMax(0.0, b.bounds.left() - textLeft);
            continue;
        }
        if (b.kind != DocxBlock::Kind::Paragraph) continue;
        const double leftGap  = b.bounds.left() - textLeft;
        const double rightGap = textRight - b.bounds.right();

        // Centred and right-aligned text is given indents that put its middle or
        // its end exactly where it was, whatever was left over from the guess.
        if (leftGap > 6.0 && rightGap > 6.0
                && qAbs(leftGap - rightGap) < qMax(4.0, (leftGap + rightGap) * 0.06)) {
            b.align = Qt::AlignHCenter;
            b.indentPt      = qMax(0.0, leftGap - rightGap);
            b.rightIndentPt = wrapSlack + qMax(0.0, rightGap - leftGap);
        } else if (leftGap > 6.0 && rightGap < 3.0 && b.lines.size() == 1) {
            b.align = Qt::AlignRight;
            b.rightIndentPt = wrapSlack + qMax(0.0, rightGap);
        } else {
            b.align    = Qt::AlignLeft;
            b.indentPt = qMax(0.0, leftGap);
            b.slackPt  = wrapSlack;
            // A wrapped paragraph keeps the width it was wrapped to, so text set
            // beside a picture stays beside it.
            if (b.lines.size() > 1) b.rightIndentPt = qMax(0.0, rightGap);
        }
    }
    return blocks;
}
