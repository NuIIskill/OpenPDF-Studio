#include "engine/edit/ContentMap.hpp"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════════════
//  Backend-neutral part: font resolution, cluster classification, spatial lookup
// ═══════════════════════════════════════════════════════════════════════════════

// ── Font resolution ───────────────────────────────────────────────────────────

ResolvedFont resolvePdfFont(const QString &rawBaseFont)
{
    ResolvedFont out;
    if (rawBaseFont.isEmpty()) return out;

    QString n = rawBaseFont;
    // Strip subset prefix "ABCDEF+FontName"
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

    // Family = part before the first '-' or ',' (style/subfamily separator)
    int cut = n.indexOf(u'-');
    const int cut2 = n.indexOf(u',');
    if (cut2 >= 0 && (cut < 0 || cut2 < cut)) cut = cut2;
    QString fam = (cut > 0) ? n.left(cut) : n;

    // Alias map for the standard-14 families and their common clones.
    const QString famLow = fam.toLower();
    if (famLow.contains(QLatin1String("helvetica")))     { out.family = QStringLiteral("Helvetica");       return out; }
    if (famLow.contains(QLatin1String("arial")))          { out.family = QStringLiteral("Arial");           return out; }
    if (famLow.contains(QLatin1String("times")))          { out.family = QStringLiteral("Times New Roman"); return out; }
    if (famLow.contains(QLatin1String("courier")))        { out.family = QStringLiteral("Courier New");     return out; }
    if (famLow.contains(QLatin1String("symbol")))         { out.family = QStringLiteral("Symbol");          return out; }
    if (famLow.contains(QLatin1String("zapf")))           { out.family = QStringLiteral("ZapfDingbats");    return out; }

    // Strip glued style suffixes ("VerdanaBold" → "Verdana")
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

    // Camel-case split: "MinionPro" → "Minion Pro", "TrebuchetMS" → "Trebuchet MS"
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

// ── Cluster classification ────────────────────────────────────────────────────

namespace {

struct Seg {
    QRectF  bounds;
    QString text;
    int     firstIdx { -1 };   // index into cluster list (style source)
    QPointF origin;            // pen origin of the leftmost run in the segment
};

struct RowSegs {
    double     y     { 0.0 };
    double     lineH { 0.0 };
    QList<Seg> segs;
    bool       table { false };
};

} // namespace

QList<ContentItem> classifyContentClusters(QList<ContentCluster> clusters,
                                           bool mergeVertical)
{
    // Drop empties and degenerate runs
    QList<ContentCluster> cl;
    cl.reserve(clusters.size());
    for (const ContentCluster &c : clusters)
        if (!c.text.trimmed().isEmpty() && c.bounds.height() > 0.5)
            cl.append(c);
    if (cl.isEmpty()) return {};

    std::sort(cl.begin(), cl.end(), [](const ContentCluster &a, const ContentCluster &b) {
        if (std::abs(a.bounds.center().y() - b.bounds.center().y()) > 3.0)
            return a.bounds.center().y() < b.bounds.center().y();
        return a.bounds.left() < b.bounds.left();
    });

    // Phase 1: group runs into visual lines by center-Y proximity
    struct Row { QList<int> idx; double refY { 0.0 }; double lineH { 0.0 }; };
    QList<Row> rows;
    for (int i = 0; i < cl.size(); ++i) {
        const double cy = cl[i].bounds.center().y();
        const double lh = std::max({ cl[i].fontSizePt,
                                     cl[i].bounds.height() * 0.8, 5.0 });
        bool placed = false;
        for (Row &row : rows) {
            if (std::abs(cy - row.refY) < std::max(row.lineH, lh) * 0.6) {
                row.idx.append(i);
                row.refY  = (row.refY * (row.idx.size() - 1) + cy) / row.idx.size();
                row.lineH = std::max(row.lineH, lh);
                placed = true;
                break;
            }
        }
        if (!placed)
            rows.append(Row{ { i }, cy, lh });
    }

    // Phase 2: split each line into segments at column-sized horizontal gaps
    QList<RowSegs> rsegs;
    rsegs.reserve(rows.size());
    for (Row &row : rows) {
        std::sort(row.idx.begin(), row.idx.end(), [&](int a, int b) {
            return cl[a].bounds.left() < cl[b].bounds.left();
        });
        RowSegs rs;
        rs.y     = row.refY;
        rs.lineH = row.lineH;

        Seg cur;
        for (int k = 0; k < row.idx.size(); ++k) {
            const ContentCluster &c = cl[row.idx[k]];
            if (cur.firstIdx < 0) {
                cur = Seg{ c.bounds, c.text, row.idx[k], c.origin };
                continue;
            }
            const double gap = c.bounds.left() - cur.bounds.right();
            if (gap >= std::max(row.lineH * 1.5, 10.0)) {
                rs.segs.append(cur);
                cur = Seg{ c.bounds, c.text, row.idx[k], c.origin };
            } else {
                if (gap > row.lineH * 0.18 && !cur.text.endsWith(u' ')
                        && !c.text.startsWith(u' '))
                    cur.text += u' ';
                cur.text  += c.text;
                cur.bounds = cur.bounds.united(c.bounds);
            }
        }
        if (cur.firstIdx >= 0) rs.segs.append(cur);
        rsegs.append(rs);
    }

    std::sort(rsegs.begin(), rsegs.end(),
              [](const RowSegs &a, const RowSegs &b) { return a.y < b.y; });

    // A bullet and its following text are one list line, not two table
    // columns. Without this normalization, several adjacent bullet rows meet
    // the table-alignment heuristic and become a bogus two-column table.
    for (RowSegs &row : rsegs) {
        if (row.segs.size() < 2) continue;
        const QString marker = row.segs.first().text.trimmed();
        if (marker != QStringLiteral("•") && marker != QStringLiteral("-")
                && marker != QStringLiteral("–") && marker != QStringLiteral("—")
                && marker != QStringLiteral("▪") && marker != QStringLiteral("◦"))
            continue;
        Seg merged = row.segs.first();
        // Font/style must come from the list text, not from the tiny OpenSymbol
        // bullet glyph (typically 3-4 pt bounding height).
        merged.firstIdx = row.segs[1].firstIdx;
        for (int n = 1; n < row.segs.size(); ++n) {
            merged.text += u' ' + row.segs[n].text;
            merged.bounds = merged.bounds.united(row.segs[n].bounds);
        }
        row.segs = { merged };
    }

    // Phase 3: table detection — a multi-segment row is a table row only when a
    // vertically adjacent row is also multi-segment with ≥2 aligned column lefts.
    // (An isolated two-part line — e.g. heading + page number — stays plain text.)
    // Export keeps visual lines separate and can therefore accept the wider
    // row pitch common in roomy business tables. The editor keeps the tighter
    // threshold to avoid turning unrelated nearby labels into clickable cells.
    const double tableRowReach = mergeVertical ? 2.4 : 3.4;
    for (int i = 0; i < rsegs.size(); ++i) {
        if (rsegs[i].segs.size() < 2) continue;
        for (int j = i + 1; j < rsegs.size(); ++j) {
            const double dy = rsegs[j].y - rsegs[i].y;
            if (dy > std::max(rsegs[i].lineH, rsegs[j].lineH) * tableRowReach) break;
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

    // Phase 4: emit one item per segment
    QList<ContentItem> items;
    for (const RowSegs &rs : rsegs) {
        for (const Seg &seg : rs.segs) {
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
            if (it.isValid() && !it.text.isEmpty())
                items.append(std::move(it));
        }
    }

    if (!mergeVertical) {
        std::sort(items.begin(), items.end(), [](const ContentItem &a, const ContentItem &b) {
            if (std::abs(a.bounds.top() - b.bounds.top()) > 0.5)
                return a.bounds.top() < b.bounds.top();
            return a.bounds.left() < b.bounds.left();
        });
        return items;
    }

    // Phase 5: vertical merge — consecutive aligned Text lines become a
    // Paragraph; tightly stacked TableCell lines become one multi-line cell.
    // Search a small window of recent items so multi-column layouts merge
    // per column instead of blocking on interleaved sort order.
    std::sort(items.begin(), items.end(), [](const ContentItem &a, const ContentItem &b) {
        if (std::abs(a.bounds.top() - b.bounds.top()) > 0.5)
            return a.bounds.top() < b.bounds.top();
        return a.bounds.left() < b.bounds.left();
    });

    QList<ContentItem> merged;
    // Top of the line most recently merged into each item — the reference for
    // the block's own line spacing, which the bounds alone no longer show once
    // several lines have been united.
    QList<double> lastLineTop;
    for (ContentItem &item : items) {
        ContentItem *host = nullptr;
        const int windowStart = std::max(0, int(merged.size()) - 12);
        for (int k = merged.size() - 1; k >= windowStart; --k) {
            ContentItem &p = merged[k];
            const bool textPair = (p.type == ContentItem::Type::Text
                                   || p.type == ContentItem::Type::Paragraph)
                                  && item.type == ContentItem::Type::Text;
            const bool cellPair = p.type == ContentItem::Type::TableCell
                                  && item.type == ContentItem::Type::TableCell;
            if (!textPair && !cellPair) continue;

            const double fs = std::max({ p.fontSizePt, item.fontSizePt, 6.0 });
            const double vGap = item.bounds.top() - p.bounds.bottom();
            if (vGap < -fs * 0.4 || vGap > fs * 1.0) continue;

            const bool leftAligned = std::abs(p.bounds.left() - item.bounds.left())
                                     < fs * 1.2;
            const bool centered = std::abs(p.bounds.center().x()
                                           - item.bounds.center().x()) < fs * 1.5;
            if (!leftAligned && !centered) continue;

            // Columns must overlap horizontally — guards diagonal merges.
            const double xOverlap = std::min(p.bounds.right(), item.bounds.right())
                                  - std::max(p.bounds.left(), item.bounds.left());
            if (xOverlap < 1.0) continue;

            if (p.fontSizePt > 0.0 && item.fontSizePt > 0.0) {
                const double ratio = std::min(p.fontSizePt, item.fontSizePt)
                                   / std::max(p.fontSizePt, item.fontSizePt);
                if (ratio < 0.75) continue;
            }
            if (!p.rawFontName.isEmpty() && !item.rawFontName.isEmpty()
                    && p.rawFontName != item.rawFontName)
                continue;

            host = &p;
            break;
        }

        if (host) {
            const int hi = int(host - merged.constData());
            const double step = item.bounds.top() - lastLineTop[hi];
            if (step > 0.5)
                host->lineSpacingPt = host->lineSpacingPt > 0.0
                                          ? (host->lineSpacingPt + step) / 2.0
                                          : step;
            lastLineTop[hi] = item.bounds.top();
            host->bounds = host->bounds.united(item.bounds);
            host->text  += u'\n' + item.text;
            if (host->type == ContentItem::Type::Text)
                host->type = ContentItem::Type::Paragraph;
        } else {
            lastLineTop.append(item.bounds.top());
            merged.append(std::move(item));
        }
    }

    return merged;
}

// ── Spatial lookup ────────────────────────────────────────────────────────────

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

    // Pass 1: exact containment (3 pt tolerance); priority, then smaller area
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

    // Pass 2: nearest edge within maxDistance, biased toward fields and cells
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

// ═══════════════════════════════════════════════════════════════════════════════
//  qpdf content-stream scanner
// ═══════════════════════════════════════════════════════════════════════════════
