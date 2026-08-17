#include "engine/edit/DocxLayout.hpp"

#include <QHash>
#include <QPainter>
#include <QSet>
#include <QStack>
#include <QtMath>

#include <algorithm>
#include <cstdio>
#include <cmath>

// Set OPENPDF_DEBUG_LAYOUT=1 to print the block model the writer will receive.
static bool layoutDebug()
{
    static const bool on = !qEnvironmentVariable("OPENPDF_DEBUG_LAYOUT").isEmpty();
    return on;
}

namespace {

// ── raster access ─────────────────────────────────────────────────────────────

// Qt PDF renders onto a TRANSPARENT background — blank paper reads as
// rgba(0,0,0,0) and would count as pitch black. Composite once up front so
// every later test can treat the buffer as plain opaque RGB.
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

// The page's paper colour. Sampled from the extreme margins, which are blank on
// virtually every business document; the mode guards the odd full-bleed design.
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

// ── small numeric helpers ─────────────────────────────────────────────────────

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

// ── table assembly ────────────────────────────────────────────────────────────

struct RawTable {
    QList<ContentItem> items;
    QRectF bounds;
    // True once the horizontal extent has been measured off the shading rather
    // than guessed from the text. The measured edge is exact, so the outward
    // padding a guess needs must not be added on top of it.
    bool   measured { false };
};

// Splits items into visual rows by baseline proximity.
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
            const double tol = qMax(row.first().bounds.height(),
                                    item.bounds.height()) * 0.6;
            if (qAbs(row.first().bounds.center().y() - item.bounds.center().y()) <= tol) {
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

// Groups the classifier's TableCell items back into rectangular tables. The
// classifier already proved these lines form aligned columns; what is missing
// is which cells belong to the same table.
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

    // A single row never proves a table. Lone rows go back to the text pool.
    tables.erase(std::remove_if(tables.begin(), tables.end(), [](const RawTable &t) {
                     return clusterRows(t.items).size() < 2;
                 }), tables.end());
    return tables;
}

// The classifier only types a line as TableCell when a neighbouring row shares
// its column lefts. A centred header row ("Merkmal | Beschreibung | Praktischer
// Hinweis" over left-aligned body text) fails that test, and so does a last row
// separated by a slightly larger gap — both were exported as loose paragraphs
// while the rest of their table became a real grid. Pull them back in.
void absorbRows(RawTable &table, const QList<ContentItem> &pool,
                QList<bool> &taken)
{
    for (bool grew = true; grew; ) {
        grew = false;
        const double lineH = medianHeight(table.items);

        // Rows are re-derived from the free pool on every pass so that a row
        // pulled in now can carry the next one within reach.
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
            // A full row of the grid, or a wrapped continuation line sitting
            // directly under its own cell. A lone distant line is a heading.
            // A continuation is the second line of a cell and sits practically
            // on top of it. At the old 1.2 the first label of the chart panel
            // below a table was within reach and got pulled into the grid.
            const bool fullRow      = row.size() >= 2 && gap <= lineH * 2.8;
            const bool continuation = row.size() == 1 && gap <= lineH * 0.6;
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

// Expands the table's horizontal extent to where its shading actually ends.
// Text bounds stop at the last glyph, so column widths taken from text alone
// make the exported table visibly narrower than the original.
void refineTableExtent(const Raster &erased, QRgb paper, RawTable &table,
                       const QRectF &pageText)
{
    if (erased.isNull()) return;
    // The seed covers the body rows only. The one scanline that spans the whole
    // table uncut is the coloured header band, which sits just above them — so
    // the search reaches past the seed upwards. It deliberately does not reach
    // down: below a table is usually the next element, and a wide panel there
    // was being measured as if it were part of the grid.
    const double lineH = medianHeight(table.items);
    const QRect box = erased.toPx(table.bounds.adjusted(0.0, -lineH * 3.0,
                                                        0.0,  lineH * 0.5));
    if (box.isEmpty()) return;

    // Collect every scanline whose fill differs from the paper — shaded header,
    // banded rows. Each contributes the run it sits in.
    // A band row is recognised by being mostly non-paper across the page's text
    // column, and its extent is its outermost non-paper pixel. Growing a run
    // outwards from a probe pixel instead broke on the holes the text erase
    // leaves in a header band — the run stopped at the first erased word and
    // reported the table several points too narrow.
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
        if (first < 0 || inked * 10 < (spanR - spanL) * 6) continue;  // not a band
        if (last - first > bestRun) { bestRun = last - first; bestLeft = first; bestRight = last; }
    }
    if (bestRun <= box.width() / 2) return;   // nothing convincing found

    const QRectF found = erased.toPt(QRect(QPoint(bestLeft, box.top()),
                                           QPoint(bestRight, box.bottom())));
    // Only ever grow, and only out to the page's own text extent — a band that
    // somehow measured wider than anything else on the page is not this table.
    // No further growth cap: the band is a measurement, and capping it by a
    // guessed maximum left the widest tables a good 30 pt short.
    const double limitL = pageText.isNull() ? found.left()
                                            : qMax(found.left(), pageText.left());
    const double limitR = pageText.isNull() ? found.right()
                                            : qMin(found.right(), pageText.right());
    table.bounds.setLeft (qMin(table.bounds.left(),  limitL));
    table.bounds.setRight(qMax(table.bounds.right(), limitR));
    table.measured = true;
}

DocxBlock buildTableBlock(const RawTable &raw)
{
    QList<QList<ContentItem>> rows = clusterRows(raw.items);
    if (rows.size() < 2) return {};

    QList<double> sizes;
    for (const ContentItem &c : raw.items) sizes.append(fontSizeOf(c));
    const double fs = qMax(6.0, median(sizes));

    // A wrapped cell line ("…ranzige Kerne" / "aussortieren") clusters as a row
    // of its own. Folded back into the row above it becomes what it is: the
    // second line of one cell.
    for (int r = rows.size() - 1; r >= 1; --r) {
        QRectF above, here;
        for (const ContentItem &c : rows[r - 1]) above = above.united(c.bounds);
        for (const ContentItem &c : rows[r])     here  = here.united(c.bounds);
        if (rows[r].size() >= rows[r - 1].size()) continue;
        if (here.top() - above.bottom() > fs * 0.6) continue;
        rows[r - 1].append(rows[r]);
        std::sort(rows[r - 1].begin(), rows[r - 1].end(),
                  [](const ContentItem &a, const ContentItem &b) {
            return a.bounds.left() < b.bounds.left();
        });
        rows.removeAt(r);
    }

    // Columns are found as vertical whitespace running the height of the table,
    // not from left edges: header cells are usually centred and would each
    // invent a column of their own. Occupancy is counted per row so a single
    // cell spanning two columns cannot hide the separator between them.
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
    // Tolerating one row lets a genuinely spanning cell bridge a separator, but
    // on a short table one long cell is a large share of the votes and erases
    // the separator for everybody — there, demand the gap be completely clear.
    const int tolerated = rows.size() >= 8 ? rows.size() / 6 : 0;

    // The outer edges are the measured band where one exists — padding a
    // measured edge outward by half an em put two tables on the same page a
    // few points apart, which reads as a broken left margin.
    QList<double> edges;
    edges.append(raw.measured ? raw.bounds.left() : x0 + fs * 0.5);
    for (int i = 1; i < bins; ) {
        if (occupancy[i] > tolerated) { ++i; continue; }
        int j = i;
        while (j < bins && occupancy[j] <= tolerated) ++j;
        const double gapPt = j - i;
        // A separator is wide (never mere word spacing) and has content on
        // both sides — the blank margin before the first column is not one.
        if (gapPt >= fs * 0.9 && j < bins && occupancy[i - 1] > tolerated)
            edges.append(x0 + (i + j) / 2.0);
        i = j;
    }
    edges.append(qMax(raw.measured ? raw.bounds.right() : x1 - fs * 0.5,
                      edges.last() + fs));
    const int cols = edges.size() - 1;
    if (cols < 1) return {};

    // Assign by cell CENTRE: a centred header lands in its own column, a
    // left-aligned body cell in the same one.
    const auto columnOf = [&](const ContentItem &cell) {
        const double cx = cell.bounds.center().x();
        for (int c = 0; c < cols; ++c)
            if (cx >= edges[c] && cx < edges[c + 1]) return c;
        return cx < edges.first() ? 0 : cols - 1;
    };

    // Row boundaries run midway between the text of consecutive rows, so the
    // exported rows keep the PDF's pitch. Without explicit heights Word packs
    // them to the text and everything below the table creeps upwards.
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

    DocxBlock block;
    block.kind   = DocxBlock::Kind::Table;
    block.bounds = QRectF(edges.first(), rowEdges.first(),
                          edges.last() - edges.first(),
                          rowEdges.last() - rowEdges.first());
    block.table.rowCount = rows.size();
    for (int c = 0; c < cols; ++c)
        block.table.colWidthsPt.append(qMax(8.0, edges[c + 1] - edges[c]));
    for (int r = 0; r < rows.size(); ++r)
        block.table.rowHeightsPt.append(qMax(6.0, rowEdges[r + 1] - rowEdges[r]));

    for (int r = 0; r < rows.size(); ++r) {
        QSet<int> filled;
        QSet<int> occupied;              // columns this row already has text in
        for (const ContentItem &cell : rows[r]) occupied.insert(columnOf(cell));
        for (const ContentItem &cell : rows[r]) {
            DocxCell dc;
            dc.item = cell;
            dc.row  = r;
            dc.col  = columnOf(cell);
            // A wrapped continuation line shares its cell with the line above
            // instead of claiming a row of its own.
            if (filled.contains(dc.col)) {
                for (DocxCell &existing : block.table.cells)
                    if (existing.row == r && existing.col == dc.col) {
                        existing.item.text += u' ' + cell.text;
                        break;
                    }
                continue;
            }
            filled.insert(dc.col);
            // A cell genuinely covering the next column spans it. Measured as a
            // share of that column's width, not as "reaches past the edge": a
            // cell ending a hair beyond the boundary used to swallow the column
            // next to it, and the cell already sitting there vanished from the
            // output entirely.
            for (int c = dc.col + 1; c < cols; ++c) {
                if (occupied.contains(c)) break;   // that column has its own text
                const double covered = qMin(cell.bounds.right(), edges[c + 1])
                                     - qMax(cell.bounds.left(), edges[c]);
                if (covered < (edges[c + 1] - edges[c]) * 0.4) break;
                ++dc.colSpan;
            }
            if (cell.bgColor.isValid()) dc.shading = cell.bgColor;
            block.table.cells.append(dc);
        }
    }

    // Alignment is decided per column, never per cell. A short entry in a wide
    // column has generous space on both sides and looks centred on its own,
    // which had half the body cells drifting to the middle. What settles it is
    // whether the column's entries share a left edge or a centre line.
    for (int c = 0; c < cols; ++c) {
        QList<DocxCell *> body, header;
        for (DocxCell &cell : block.table.cells) {
            if (cell.col != c || cell.colSpan != 1) continue;
            (cell.row == 0 ? header : body).append(&cell);
        }
        const auto spread = [](const QList<DocxCell *> &group, bool centre) {
            double lo = 1e18, hi = -1e18;
            for (const DocxCell *cell : group) {
                const double v = centre ? cell->item.bounds.center().x()
                                        : cell->item.bounds.left();
                lo = qMin(lo, v);
                hi = qMax(hi, v);
            }
            return hi - lo;
        };
        if (body.size() >= 2 && spread(body, true) + fs * 0.5 < spread(body, false))
            for (DocxCell *cell : body) cell->align = Qt::AlignHCenter;

        // The header is judged against its own column's body rather than
        // against the estimated column edges: whether it is indented from the
        // text below it is a fact, where exactly the column boundary runs is a
        // guess, and hanging the decision on the guess flipped headers to left
        // alignment whenever the estimate was a few points off.
        if (body.isEmpty()) continue;
        double bodyLeft = 1e18;
        for (const DocxCell *b : body) bodyLeft = qMin(bodyLeft, b->item.bounds.left());
        // Centred is the only alternative considered. Right-aligned headers are
        // rare, and trying to spot them mistook every column whose longest body
        // entry happened to be short for a right-aligned one.
        for (DocxCell *cell : header)
            if (cell->item.bounds.left() - bodyLeft >= fs * 1.2)
                cell->align = Qt::AlignHCenter;
    }
    return block;
}

// ── paragraph assembly ────────────────────────────────────────────────────────

// Consecutive lines of one visual column, same style, normal leading — that is
// one paragraph. Word then reflows it like any typed text, which is the whole
// point of the export.
QList<DocxBlock> buildParagraphs(const QList<ContentItem> &lines)
{
    QList<ContentItem> sorted = lines;
    std::sort(sorted.begin(), sorted.end(),
              [](const ContentItem &a, const ContentItem &b) {
        if (qAbs(a.bounds.top() - b.bounds.top()) > 1.0)
            return a.bounds.top() < b.bounds.top();
        return a.bounds.left() < b.bounds.left();
    });

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
                    && sameTextStyle(tail, line)) {
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

// ── graphic region detection ──────────────────────────────────────────────────

struct Region {
    QRect  px;          // bounding box in raster pixels
    int    inkCells { 0 };
};

// Coarse connected-component scan over everything still inked once all
// recognised text has been painted out and every table area claimed. What
// survives is genuine artwork: rules, logos, chart bars, shaded panels.
QList<Region> findGraphicRegions(const Raster &erased, QRgb paper,
                                 const QList<QRectF> &claimed)
{
    QList<Region> regions;
    if (erased.isNull()) return regions;

    const int cell = qMax(1, qRound(2.0 * erased.scale()));   // ~2 pt grid
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
    // Claimed areas (text lines, tables) are reproduced natively and must not
    // also be baked into a picture.
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
                // 8-connectivity with a 2-cell reach: dashed rules and the gaps
                // between chart bars must not split one drawing into dozens.
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

    // Drop specks: antialiasing fringes left behind by the text erase. Judged
    // by area, so a legitimately thin full-width rule survives while a two-by-
    // eight-point crumb of a stripped glyph does not.
    regions.erase(std::remove_if(regions.begin(), regions.end(),
                                 [&](const Region &r) {
                                     const double w = r.px.width()  / erased.scale();
                                     const double h = r.px.height() / erased.scale();
                                     return r.inkCells < 2 || w * h < 36.0;
                                 }),
                  regions.end());
    return regions;
}

// A region that is one flat colour (optionally with a frame around it) is a
// shaded box — a callout, a header band, a table-like panel. Those become real
// shaded paragraphs instead of pictures, so the text inside stays editable.
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
    if (double(bestN) / total < 0.55) return false;        // too busy — artwork

    // Everything that is neither the fill nor a claimed text area must sit on
    // the perimeter (a border). Ink in the interior means real graphics.
    const int band = qMax(2, qRound(2.5 * erased.scale()));
    const QRect inner = box.adjusted(band, band, -band, -band);
    int strayInterior = 0, interior = 0;
    for (int y = box.top(); y <= box.bottom(); y += step) {
        for (int x = box.left(); x <= box.right(); x += step) {
            const QRgb c = erased.at(x, y);
            if (colorDist(c, fill) <= 40) continue;
            if (!inner.contains(x, y)) continue;   // a frame, not content
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

} // namespace

// ── entry point ───────────────────────────────────────────────────────────────

QList<DocxBlock> buildDocxBlocks(const DocxLayoutInput &in, QMarginsF *marginsOut)
{
    QList<ContentItem> text, cells;
    for (const ContentItem &item : in.items) {
        if (!item.isTextual() || item.text.trimmed().isEmpty()) continue;
        if (item.type == ContentItem::Type::TableCell) cells.append(item);
        else                                           text.append(item);
    }
    if (text.isEmpty() && cells.isEmpty()) return {};

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
    const Raster original(in.original, in.scale);
    const QRgb   paper = paperColor(erased);

    // ── tables ────────────────────────────────────────────────────────────────
    // Everything the page writes on. Used to keep a table's raster-measured
    // extent inside the area the document itself occupies.
    QRectF pageText;
    for (const ContentItem &i : text)  pageText = pageText.united(i.bounds);
    for (const ContentItem &i : cells) pageText = pageText.united(i.bounds);

    QList<RawTable> raw = seedTables(cells);
    // The extent is widened from the shading BEFORE neighbouring rows are
    // pulled in: a header cell of the outermost column ("Punkte") sits past the
    // last body cell, and against the un-widened bounds it reads as outside the
    // table and stays behind as a stray paragraph.
    for (RawTable &t : raw) refineTableExtent(erased, paper, t, pageText);

    QList<bool> taken(text.size(), false);
    for (RawTable &t : raw) absorbRows(t, text, taken);

    QList<QRectF>    claimed;
    QList<QRectF>    tableAreas;
    QList<DocxBlock> blocks;
    for (RawTable &t : raw) {
        DocxBlock block = buildTableBlock(t);
        if (block.table.colWidthsPt.isEmpty()) continue;
        // The grid — shading, rules and all — is reproduced natively. Its area
        // is deliberately NOT masked out of the ink map: left intact, the whole
        // decoration forms one connected region that is then recognised as
        // belonging to the table and dropped. Masking it instead left slivers
        // of band around the edges, and each sliver came back as a picture.
        blocks.append(block);
        tableAreas.append(block.bounds.united(t.bounds));
    }

    // Cells of a rejected single-row "table" are ordinary lines after all.
    for (const ContentItem &c : cells) {
        bool inTable = false;
        for (const QRectF &area : tableAreas)
            if (area.contains(c.bounds.center())) { inTable = true; break; }
        if (!inTable) text.append(c);
    }
    // Items absorbed into a grid must not also flow as paragraphs.
    {
        QList<ContentItem> rest;
        for (int i = 0; i < text.size(); ++i)
            if (i >= taken.size() || !taken[i]) rest.append(text[i]);
        text = rest;
    }
    for (const ContentItem &t : text)
        claimed.append(t.bounds.adjusted(-1.0, -1.0, 1.0, 1.0));

    // ── graphics ──────────────────────────────────────────────────────────────
    // Regions are classified before paragraphs are built: a flat shaded panel
    // becomes a background for the text inside it, while real artwork swallows
    // the text it contains (chart labels belong to the chart, not to the flow).
    QSet<int>  bakedText;
    QSet<int>  droppedTables;
    QList<DocxBlock> pictures;
    for (const Region &region : findGraphicRegions(erased, paper, claimed)) {
        QRgb fill = paper;
        const QRectF boundsPt = erased.toPt(region.px);
        const double regionArea = boundsPt.width() * boundsPt.height();

        // A region and a table that occupy the same spot are one of two things,
        // and getting the two confused is what exported the same content twice.
        //   • region ≈ table  → it is the table's own shading and rules, already
        //     expressed as w:shd and w:tblBorders. Drop the region.
        //   • region ≫ table  → artwork that happens to contain aligned text
        //     (a process diagram of five labelled boxes). Drop the table and
        //     keep the drawing, text baked in.
        bool isTableDecoration = false;
        for (int t = 0; t < tableAreas.size(); ++t) {
            const QRectF hit = tableAreas[t].intersected(boundsPt);
            const double share = hit.width() * hit.height();
            const double tableArea = tableAreas[t].width() * tableAreas[t].height();
            if (tableArea <= 0.0 || share <= 0.0) continue;
            // Sitting inside the table (a rule, a fill-in underscore, the band
            // itself) — the table already draws it.
            if (share >= regionArea * 0.8 && regionArea <= tableArea * 1.6) {
                isTableDecoration = true;
                break;
            }
            // Containing the table and much larger — artwork, not a grid.
            if (share >= tableArea * 0.25 && regionArea > tableArea * 1.6)
                droppedTables.insert(t);
        }
        if (isTableDecoration) continue;
        if (isFlatFill(erased, paper, region.px, claimed, &fill)) {
            // Flat panel: keep the text editable, carry the fill as shading on
            // the lines that sit inside it.
            for (ContentItem &item : text)
                if (boundsPt.contains(item.bounds.center()))
                    item.bgColor = QColor::fromRgb(fill);
            continue;
        }
        DocxBlock pic;
        pic.kind    = DocxBlock::Kind::Picture;
        pic.bounds  = boundsPt;
        pic.picture = original.crop(region.px);
        if (pic.picture.isNull()) continue;
        pictures.append(pic);

        // Text inside a picture is only baked into it when the picture is a
        // solid thing — a chart, a filled panel, a photo — where leaving the
        // labels to the flow would misplace them. Sparse line art (a plain
        // bordered table, a set of rules) covers the same area with a fraction
        // of the ink; baking there would turn every unshaded table on the page
        // into an uneditable image of itself. The drawing sits behind the text
        // either way, so keeping the text loses nothing.
        const int cellPx  = qMax(1, qRound(2.0 * in.scale));
        const int gridArea = qMax(1, (region.px.width()  / cellPx)
                                   * (region.px.height() / cellPx));
        if (double(region.inkCells) / gridArea < 0.25) continue;

        for (int i = 0; i < text.size(); ++i)
            if (boundsPt.contains(text[i].bounds.center())) bakedText.insert(i);
    }

    // Tables that turned out to be artwork are already drawn inside their
    // picture; their items were taken out of the text pool earlier, so simply
    // removing the block leaves the diagram intact and unduplicated.
    for (int t = tableAreas.size() - 1; t >= 0; --t)
        if (droppedTables.contains(t) && t < blocks.size()) blocks.removeAt(t);
    blocks.append(pictures);

    QList<ContentItem> flowing;
    for (int i = 0; i < text.size(); ++i)
        if (!bakedText.contains(i)) flowing.append(text[i]);

    // ── paragraphs ────────────────────────────────────────────────────────────
    blocks.append(buildParagraphs(flowing));

    std::sort(blocks.begin(), blocks.end(),
              [](const DocxBlock &a, const DocxBlock &b) {
        if (qAbs(a.bounds.top() - b.bounds.top()) > 1.0)
            return a.bounds.top() < b.bounds.top();
        return a.bounds.left() < b.bounds.left();
    });

    // ── margins and alignment ─────────────────────────────────────────────────
    QRectF content;
    for (const DocxBlock &b : blocks)
        if (b.kind != DocxBlock::Kind::Picture) content = content.united(b.bounds);
    if (content.isNull())
        for (const DocxBlock &b : blocks) content = content.united(b.bounds);

    // Blocks are positioned by explicit spacing measured from the top, so the
    // bottom margin only decides where Word breaks the page. Kept at the PDF's
    // own value it breaks a hair too early — the footer line renders a shade
    // taller than its glyphs and lands on a page of its own, leaving the real
    // next page empty. A deliberately small value gives the flow that slack.
    QMarginsF margins(qBound(12.0, content.left(), in.pageSizePt.width() / 3.0),
                      qBound(12.0, content.top(),  in.pageSizePt.height() / 3.0),
                      qBound(12.0, in.pageSizePt.width() - content.right(),
                             in.pageSizePt.width() / 3.0),
                      12.0);
    if (marginsOut) *marginsOut = margins;

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
        // Centred only when both sides are inset by a comparable, real amount;
        // otherwise a short last line would be mistaken for a centred one.
        if (leftGap > 6.0 && rightGap > 6.0
                && qAbs(leftGap - rightGap) < qMax(6.0, (leftGap + rightGap) * 0.22)) {
            b.align = Qt::AlignHCenter;
        } else if (leftGap > 6.0 && rightGap < 3.0 && b.lines.size() == 1) {
            b.align = Qt::AlignRight;
        } else {
            b.align    = Qt::AlignLeft;
            b.indentPt = qMax(0.0, leftGap);
        }
    }
    return blocks;
}
