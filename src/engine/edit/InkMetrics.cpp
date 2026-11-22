#include "engine/edit/InkMetrics.hpp"

#include <QFontMetricsF>
#include <QMap>
#include <QPainter>
#include <QStringList>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

inline QRgb pixelOverWhite(const QImage &img, int x, int y)
{
    const QRgb c = img.pixel(x, y);
    const int  a = qAlpha(c);
    if (a == 255) return c;
    return qRgb((qRed(c)   * a + 255 * (255 - a)) / 255,
                (qGreen(c) * a + 255 * (255 - a)) / 255,
                (qBlue(c)  * a + 255 * (255 - a)) / 255);
}

inline int lumAt(const QImage &img, int x, int y)
{
    const QRgb c = pixelOverWhite(img, x, y);
    return (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000;
}

int backgroundLuminance(const QImage &img, const QRect &sr)
{
    QMap<int, int> hist;
    for (int y = sr.top(); y <= sr.bottom(); ++y)
        for (int x = sr.left(); x <= sr.right(); ++x)
            hist[lumAt(img, x, y)]++;
    int bgLum = 255, bgN = 0;
    for (auto it = hist.cbegin(); it != hist.cend(); ++it)
        if (it.value() > bgN) { bgN = it.value(); bgLum = it.key(); }
    return bgLum;
}

struct InkRun { int top; int height; qint64 pixels; };

QList<InkRun> inkRuns(const QImage &img, const QRect &region)
{
    const QRect sr = region.intersected(img.rect());
    if (sr.width() < 4 || sr.height() < 4) return {};

    const int bgLum = backgroundLuminance(img, sr);

    QList<InkRun> runs;
    int start = -1;
    qint64 pixels = 0;
    for (int y = sr.top(); y <= sr.bottom(); ++y) {
        int ink = 0;
        for (int x = sr.left(); x <= sr.right(); ++x)
            if (std::abs(lumAt(img, x, y) - bgLum) > 40) ++ink;

        if (ink >= 2) {
            if (start < 0) { start = y; pixels = 0; }
            pixels += ink;
        } else if (start >= 0) {
            runs.append({ start, y - start, pixels });
            start = -1;
        }
    }
    if (start >= 0) runs.append({ start, sr.bottom() + 1 - start, pixels });
    return runs;
}

int inkLeftPx(const QImage &img, const QRect &region, int top, int bottom)
{
    const QRect sr = region.intersected(img.rect());
    if (sr.isEmpty()) return -1;

    const int bgLum = backgroundLuminance(img, sr);

    const int y0 = qMax(sr.top(), top);
    const int y1 = qMin(sr.bottom(), bottom);
    for (int x = sr.left(); x <= sr.right(); ++x) {
        int ink = 0;
        for (int y = y0; y <= y1; ++y)
            if (std::abs(lumAt(img, x, y) - bgLum) > 40) ++ink;
        if (ink >= 1) return x;
    }
    return -1;
}

/// Stores one contiguous ink run.
struct InkLine { int top { -1 }; int height { 0 }; int left { -1 }; };

InkLine inkLinePx(const QImage &img, const QRect &region)
{
    const QList<InkRun> runs = inkRuns(img, region);
    if (runs.isEmpty()) return {};

    const InkRun *main = &runs.first();
    for (const InkRun &r : runs)
        if (r.pixels > main->pixels) main = &r;

    InkLine out;
    out.top    = main->top;
    out.height = main->height;
    out.left   = inkLeftPx(img, region, main->top, main->top + main->height - 1);
    return out;
}

}

namespace InkMetrics {

QColor sampleTextColor(const QImage &img, const QRect &region)
{
    const QRect sr = region.intersected(img.rect());
    if (sr.isEmpty()) return QColor(0x11, 0x11, 0x11);

    struct Px { int lum; QRgb rgb; };
    std::vector<Px> pixels;
    pixels.reserve(size_t(sr.width()) * sr.height() / 4 + 16);
    for (int y = sr.top(); y <= sr.bottom(); ++y)
        for (int x = sr.left(); x <= sr.right(); ++x) {
            const QRgb c  = pixelOverWhite(img, x, y);
            const int lum = (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114)
                          / 1000;
            if (lum < 240) pixels.push_back({ lum, c });
        }
    if (pixels.empty()) return QColor(0x11, 0x11, 0x11);

    std::sort(pixels.begin(), pixels.end(),
              [](const Px &a, const Px &b) { return a.lum < b.lum; });
    const size_t take = std::max<size_t>(8, pixels.size() / 10);
    long r = 0, g = 0, b = 0;
    const size_t n = std::min(take, pixels.size());
    for (size_t i = 0; i < n; ++i) {
        r += qRed(pixels[i].rgb);
        g += qGreen(pixels[i].rgb);
        b += qBlue(pixels[i].rgb);
    }
    const QColor core(int(r / n), int(g / n), int(b / n));

    if (core.lightnessF() > 0.82) return QColor(0x11, 0x11, 0x11);
    return core;
}

QColor sampleBackgroundColor(const QImage &img, const QRect &region)
{
    const QRect sr = region.intersected(img.rect());
    if (sr.isEmpty()) return {};

    QMap<QRgb, int> counts;
    const int stepX = qMax(1, sr.width()  / 120);
    const int stepY = qMax(1, sr.height() / 40);
    for (int y = sr.top(); y <= sr.bottom(); y += stepY)
        for (int x = sr.left(); x <= sr.right(); x += stepX)
            counts[pixelOverWhite(img, x, y)]++;
    if (counts.isEmpty()) return {};

    QRgb best = 0; int bestN = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() > bestN) { bestN = it.value(); best = it.key(); }
    return QColor::fromRgb(best);
}

MeasuredInk measuredInkPt(const QImage &img, const QRectF &boundsPt, qreal scale)
{
    if (img.isNull() || boundsPt.isEmpty() || scale <= 0.0) return {};
    const auto measure = [&](const QRectF &r) -> MeasuredInk {
        const QRectF px(r.topLeft() * scale, r.size() * scale);
        const InkLine ink = inkLinePx(img, px.toAlignedRect());
        if (ink.top < 0) return {};
        return { ink.top / scale, ink.height / scale,
                 ink.left >= 0 ? ink.left / scale : r.left() };
    };

    const MeasuredInk tight = measure(boundsPt);
    if (tight.height <= 0.0) return tight;

    const double pad = qMax(1.0, boundsPt.height() * 0.25);
    const MeasuredInk padded = measure(boundsPt.adjusted(0, -pad, 0, pad));
    if (padded.height > tight.height && padded.height <= tight.height * 1.35)
        return padded;
    return tight;
}

FontInk fontInkPerPt(const QString &text, QFont f)
{
    constexpr int kRef = 64;
    f.setPixelSize(kRef);
    const QFontMetricsF fm(f);

    QList<double> heights, rises, bearings;
    const QStringList lines = text.split(u'\n');
    for (const QString &raw : lines) {

        const QString ln = raw.trimmed().left(120);
        if (ln.isEmpty()) continue;

        const int w = qBound(4 * kRef,
                             qCeil(fm.horizontalAdvance(ln)) + 2 * kRef,
                             8000);

        constexpr int kPenX = kRef / 2;
        constexpr int kBase = 2 * kRef;
        QImage probe(w, 4 * kRef, QImage::Format_RGB32);
        probe.fill(Qt::white);
        {
            QPainter p(&probe);
            p.setFont(f);
            p.setPen(Qt::black);
            p.drawText(QPointF(kPenX, kBase), ln);
        }
        const InkLine ink = inkLinePx(probe, probe.rect());
        if (ink.height > 0 && ink.top >= 0) {
            heights.append(ink.height / double(kRef));
            rises.append((kBase - ink.top) / double(kRef));
            if (ink.left >= 0)
                bearings.append((ink.left - kPenX) / double(kRef));
        }
    }
    if (heights.isEmpty()) return {};

    const auto median = [](QList<double> v) {
        if (v.isEmpty()) return 0.0;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    return { median(heights), median(rises), median(bearings) };
}


QRectF inkAroundPt(const QImage &img, const QRectF &linePt, qreal scale)
{
    // The ink of a scanned line: the rows of ink reaching into the line, as
    // far up and down as they run unbroken, and the columns they cover.
    if (img.isNull() || linePt.isEmpty() || scale <= 0.0) return linePt;
    const double h = linePt.height();
    const QRect sr = QRectF(QPointF(linePt.left() - h * 0.3, linePt.top() - h * 0.6) * scale,
                            QPointF(linePt.right() + h * 0.3, linePt.bottom() + h * 0.6) * scale)
                         .toAlignedRect().intersected(img.rect());
    if (sr.width() < 4 || sr.height() < 4) return linePt;
    const int bg = backgroundLuminance(img, sr);
    const int x0 = qMax(sr.left(), qFloor((linePt.left() - h * 0.1) * scale));
    const int x1 = qMin(sr.right(), qCeil((linePt.right() + h * 0.1) * scale));
    const auto rowInk = [&](int y) {
        int ink = 0;
        for (int x = x0; x <= x1; ++x)
            if (std::abs(lumAt(img, x, y) - bg) > 40) ++ink;
        return ink;
    };
    // Specks of paper noise between lines are not ink of the line.
    const int least = qMax(2, (x1 - x0) / 100);
    const int lineTop = qMax(sr.top(), qFloor(linePt.top() * scale));
    const int lineBottom = qMin(sr.bottom(), qCeil(linePt.bottom() * scale));
    int top = -1, bottom = -1;
    for (int y = lineTop; y <= lineBottom; ++y)
        if (rowInk(y) >= least) { if (top < 0) top = y; bottom = y; }
    if (top < 0) return linePt;
    while (top > sr.top() && rowInk(top - 1) >= least) --top;
    while (bottom < sr.bottom() && rowInk(bottom + 1) >= least) ++bottom;

    int left = -1, right = -1;
    for (int x = sr.left(); x <= sr.right(); ++x) {
        int ink = 0;
        for (int y = top; y <= bottom; ++y)
            if (std::abs(lumAt(img, x, y) - bg) > 40) ++ink;
        if (ink >= 2) { if (left < 0) left = x; right = x; }
    }
    if (left < 0) return linePt;
    return linePt.united(QRectF(QPointF(left / scale, top / scale),
                                QPointF((right + 1) / scale, (bottom + 1) / scale)));
}

// The columns of a line's sample that hold a line of a box beside a letter, as of a field with a
// figure in each box: inked from above the line's top to below its baseline, where no letter
// reaches, or between the box's own rules above and below the line where it has them, which a
// text layer's box may reach past.
static std::vector<bool> boxColumns(const QImage &img, const QRect &sr, int x0, int x1, int base, int top, int bottom,
                                    int above, int bg)
{
    std::vector<bool> boxed(size_t(sr.width()), false);
    const auto across = [&](int y) {
        int ink = 0;
        for (int x = x0; x <= x1; ++x) ink += std::abs(lumAt(img, x, y) - bg) > 40;
        return ink >= (x1 - x0 + 1) * 0.9;
    };
    int from = qMax(sr.top(), top), to = qMin(sr.bottom(), bottom);
    for (int y = base - above; y >= sr.top(); --y)
        if (across(y)) { from = y + 1; break; }
    for (int y = base + 1; y <= sr.bottom(); ++y)
        if (across(y)) { to = y - 1; break; }
    for (int x = sr.left(); x <= sr.right() && to - from >= 6; ++x) {
        int ink = 0;
        for (int y = from; y <= to; ++y) ink += std::abs(lumAt(img, x, y) - bg) > 40;
        boxed[size_t(x - sr.left())] = ink >= (to - from + 1) * 0.9;
    }
    return boxed;
}

QRectF inkOnBaselinePt(const QImage &img, const QRectF &linePt, double baselinePt, qreal scale)
{
    // The ink of a scanned line standing on its baseline: the rows of ink
    // above and below it as far as they run unbroken; where they run into the
    // line above, they are parted at its thinnest row. A text layer's box may
    // reach far into that line, or stop short of its capitals.
    if (img.isNull() || linePt.isEmpty() || scale <= 0.0) return {};
    const double h = linePt.height();
    const QRect sr = QRectF(QPointF(linePt.left() - h * 0.3, linePt.top() - h * 0.4) * scale,
                            QPointF(linePt.right() + h * 0.3, baselinePt + h * 0.6) * scale)
                         .toAlignedRect().intersected(img.rect());
    const int base = qRound(baselinePt * scale);
    if (sr.width() < 4 || base <= sr.top() || base > sr.bottom()) return {};
    const int bg = backgroundLuminance(img, sr);
    const int x0 = qMax(sr.left(), qFloor(linePt.left() * scale)), x1 = qMin(sr.right(), qCeil(linePt.right() * scale));
    const std::vector<bool> boxed = boxColumns(img, sr, x0, x1, base, qRound((linePt.top() - h * 0.1) * scale),
                                               qRound((baselinePt + h * 0.3) * scale), qRound(h * 0.3 * scale), bg);
    const auto inked = [&](int x, int y) { return !boxed[size_t(x - sr.left())] && std::abs(lumAt(img, x, y) - bg) > 40; };
    // A rule under or through the line is none of its ink: a row of ink running on past both
    // its ends, or unbroken all along a line of several letters, which their gaps would break.
    const bool several = x1 - x0 >= h * 3.0 * scale;
    const auto ruled = [&](int y) {
        const bool past = sr.left() < x0 && sr.right() > x1 && inked(sr.left(), y)
                       && inked(sr.right(), y);
        if (!past && !several) return false;
        int ink = 0;
        for (int x = x0; x <= x1; ++x)
            if (inked(x, y)) ++ink;
        return ink >= (x1 - x0 + 1) * (past ? 0.9 : 0.97);
    };
    const auto rowInk = [&](int y) {
        if (ruled(y)) return 0;
        int ink = 0;
        for (int x = x0; x <= x1; ++x)
            if (inked(x, y)) ++ink;
        return ink;
    };
    const int least = qMax(2, (x1 - x0) / 100);
    // The letters are densest just above the baseline, where their small letters stand.
    int core = -1, densest = 0;
    for (int y = qMax(sr.top(), base - qRound(h * 0.4 * scale)); y < base; ++y)
        if (const int ink = rowInk(y); ink > densest) { densest = ink; core = y; }
    if (core < 0 || densest < qMax(least, (x1 - x0) / 20)) return {};
    int top = core;
    while (top > sr.top() && rowInk(top - 1) >= least) --top;
    if (top == sr.top() && top > img.rect().top() && rowInk(top - 1) >= least) {
        int thinnest = top, inkThere = rowInk(top);
        for (int y = top; y < base - (base - top) / 2; ++y)
            if (const int ink = rowInk(y); ink < inkThere) { inkThere = ink; thinnest = y; }
        top = thinnest + 1;
    }
    // Dots and accents stand a little apart above the letters, in a short run of their own.
    const int span = base - top;
    const auto dotted = [&](int y) {
        for (int x = x0; x < x1; ++x)
            if (inked(x, y) && inked(x + 1, y)) return true;
        return false;
    };
    // Ink running out at a side is a letter's of the line above, cut where this line ends.
    const auto cut = [&](int y) { return inked(x0, y) || inked(x1, y); };
    int above = top - 1;
    while (above > sr.top() && top - above <= span * 0.3 && !dotted(above)) --above;
    if (above > sr.top() && top - above <= span * 0.3 && dotted(above)) {
        int runTop = above;
        bool own = !cut(above);
        while (runTop > sr.top() && dotted(runTop - 1)) own = !cut(--runTop) && own;
        if (own && runTop > sr.top() && above - runTop + 1 <= span * 0.3) top = runTop;
    }
    // Below the baseline only the descenders reach, a few letters of the
    // line, each hanging on ink in the row above; a speck of dust hangs on none.
    int bottom = core;
    const int lowest = qMin(sr.bottom(), base + int(span * 0.45));
    const auto hanging = [&](int y) {
        if (ruled(y)) return false;
        int n = 0;
        for (int x = x0; x <= x1; ++x) {
            if (!inked(x, y)) continue;
            for (int dx = -1; dx <= 1 && n >= 0; ++dx)
                if (x + dx >= x0 && x + dx <= x1 && inked(x + dx, y - 1)) { ++n; break; }
        }
        return n >= 2;
    };
    while (bottom < lowest && (rowInk(bottom + 1) >= least || hanging(bottom + 1))) ++bottom;
    int left = -1, right = -1;
    for (int x = sr.left(); x <= sr.right(); ++x) {
        int ink = 0;
        for (int y = top; y <= bottom; ++y)
            if (inked(x, y)) ++ink;
        if (ink >= 2) { if (left < 0) left = x; right = x; }
    }
    if (left < 0) return {};
    return QRectF(QPointF(left / scale, top / scale), QPointF((right + 1) / scale, (bottom + 1) / scale));
}

bool ruledBetweenPt(const QImage &img, const QRectF &abovePt, const QRectF &belowPt, qreal scale)
{
    // A rule between two lines, as a table draws between its rows: a row of ink
    // across where they stand one above the other.
    if (img.isNull() || scale <= 0.0) return false;
    double left = qMax(abovePt.left(), belowPt.left()), right = qMin(abovePt.right(), belowPt.right());
    if (right - left < qMin(abovePt.width(), belowPt.width()) * 0.5) {
        const QRectF &narrow = abovePt.width() < belowPt.width() ? abovePt : belowPt;
        left = narrow.left();
        right = narrow.right();
    }
    const QRect between = QRectF(QPointF(left, abovePt.bottom()) * scale, QPointF(right, belowPt.top()) * scale)
                              .toAlignedRect().intersected(img.rect());
    if (between.width() < 8 || between.height() < 1) return false;
    const int bg = backgroundLuminance(img, between.adjusted(0, -between.height(), 0, between.height()).intersected(img.rect()));
    for (int y = between.top(); y <= between.bottom(); ++y) {
        int ink = 0;
        for (int x = between.left(); x <= between.right(); ++x) ink += std::abs(lumAt(img, x, y) - bg) > 40;
        if (ink >= between.width() * 0.9) return true;
    }
    return false;
}

double strokeWidthPt(const QImage &img, const QRectF &inkPt, qreal scale)
{
    // The typical width of the strokes across: the mean of the middle half of
    // the dark runs, with the threshold halfway between paper and ink so blur
    // counts half. The long runs along horizontal strokes are left out.
    const QRect sr = QRectF(inkPt.topLeft() * scale, inkPt.size() * scale).toAlignedRect()
                         .intersected(img.rect());
    if (sr.width() < 4 || sr.height() < 4 || scale <= 0.0) return 0.0;
    const int bg = backgroundLuminance(img, sr);
    int darkest = bg;
    for (int y = sr.top(); y <= sr.bottom(); ++y)
        for (int x = sr.left(); x <= sr.right(); ++x) darkest = qMin(darkest, lumAt(img, x, y));
    if (bg - darkest < 60) return 0.0;
    const int cut = (bg + darkest) / 2;
    std::vector<int> runs;
    for (int y = sr.top(); y <= sr.bottom(); ++y) {
        int run = 0;
        for (int x = sr.left(); x <= sr.right() + 1; ++x) {
            if (x <= sr.right() && lumAt(img, x, y) < cut) { ++run; continue; }
            if (run > 0) runs.push_back(run);
            run = 0;
        }
    }
    if (runs.empty()) return 0.0;
    std::sort(runs.begin(), runs.end());
    const size_t from = runs.size() / 4, to = qMax(from + 1, runs.size() * 3 / 4);
    double sum = 0.0;
    for (size_t i = from; i < to; ++i) sum += runs[i];
    return sum / double(to - from) / scale;
}


Blob blobPt(const QImage &img, const QRectF &areaPt, qreal scale)
{
    // The pixels darker than halfway between the paper and the darkest of the
    // area, in connected pieces; pieces cut by its sides belong to the
    // neighbours, and specks far smaller than the largest piece are dust.
    const QRect r = QRectF(areaPt.topLeft() * scale, areaPt.size() * scale).toAlignedRect().intersected(img.rect());
    if (r.width() < 3 || r.height() < 2) return {};
    const int paper = backgroundLuminance(img, r);
    int darkest = 255;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x) darkest = qMin(darkest, lumAt(img, x, y));
    if (paper - darkest < 60) return {};
    const int cut = (paper + darkest) / 2;
    const int w = r.width(), h = r.height();
    std::vector<int> label(size_t(w) * size_t(h), -1);
    std::vector<bool> on(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) on[size_t(y) * size_t(w) + size_t(x)] = lumAt(img, r.left() + x, r.top() + y) < cut;
    struct Piece { QRect box; qint64 pixels { 0 }; bool cut { false }; };
    QList<Piece> pieces;
    QList<QPoint> todo;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            if (!on[i] || label[i] >= 0) continue;
            Piece piece;
            label[i] = int(pieces.size());
            todo = { QPoint(x, y) };
            while (!todo.isEmpty()) {
                const QPoint p = todo.takeLast();
                piece.box = piece.box.isNull() ? QRect(p, QSize(1, 1)) : piece.box.united(QRect(p, QSize(1, 1)));
                ++piece.pixels;
                piece.cut |= p.x() == 0 || p.x() == w - 1;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = p.x() + dx, ny = p.y() + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        const size_t n = size_t(ny) * size_t(w) + size_t(nx);
                        if (!on[n] || label[n] >= 0) continue;
                        label[n] = int(pieces.size());
                        todo.append(QPoint(nx, ny));
                    }
            }
            pieces.append(piece);
        }
    qint64 largest = 0;
    for (const Piece &piece : std::as_const(pieces))
        if (!piece.cut) largest = qMax(largest, piece.pixels);
    if (largest == 0) return {};
    QRect box;
    qint64 inked = 0;
    for (const Piece &piece : std::as_const(pieces)) {
        if (piece.cut || piece.pixels * 5 < largest) continue;
        box = box.isNull() ? piece.box : box.united(piece.box);
        inked += piece.pixels;
    }
    box.translate(r.topLeft());
    const double area = double(box.width()) * double(box.height());
    return { QRectF(box.left() / scale, box.top() / scale, box.width() / scale, box.height() / scale), inked / area };
}


double slopePx(const QImage &img, const QRect &px, int cut)
{
    // How a line on a page lying askew slopes, down per across: the shear
    // that gathers its pixels darker than the cut in the fewest, fullest rows.
    constexpr double kMost = 0.055;
    const QRect r = px.intersected(img.rect());
    QList<QPoint> dark;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x)
            if (qGray(img.pixel(x, y)) < cut) dark.append(QPoint(x - r.left(), y - r.top()));
    if (dark.size() < 50) return 0.0;
    const double mid = r.width() / 2.0;
    const int off = qCeil(kMost * mid) + 2;
    std::vector<double> rows(size_t(r.height() + 2 * off));
    const auto sharpness = [&](double t) {
        std::fill(rows.begin(), rows.end(), 0.0);
        for (const QPoint &p : std::as_const(dark)) {
            const double y = p.y() - (p.x() - mid) * t + off;
            const int y0 = qFloor(y);
            const double f = y - y0;
            rows[size_t(y0)] += 1.0 - f;
            rows[size_t(y0 + 1)] += f;
        }
        double sum = 0.0;
        for (const double v : rows) sum += v * v;
        return sum;
    };
    double best = 0.0, top = sharpness(0.0);
    for (double t = -kMost; t <= kMost + 1e-9; t += 0.0025)
        if (const double v = sharpness(t); v > top) { top = v; best = t; }
    const double coarse = best;
    for (double t = coarse - 0.0025; t <= coarse + 0.0025 + 1e-9; t += 0.00025)
        if (const double v = sharpness(t); v > top) { top = v; best = t; }
    return best;
}

double gapMiddlePt(const QImage &img, const QRectF &areaPt, qreal scale)
{
    // The middle of the widest run of columns without ink across the area; NaN where every column has some.
    const QRect r = QRectF(areaPt.topLeft() * scale, areaPt.size() * scale).toAlignedRect().intersected(img.rect());
    if (r.width() < 2 || r.height() < 2) return qQNaN();
    const int paper = backgroundLuminance(img, r);
    int darkest = 255;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x) darkest = qMin(darkest, lumAt(img, x, y));
    const int cut = paper - darkest < 60 ? -1 : (paper + darkest) / 2;
    int best = -1, bestFrom = 0, run = 0;
    for (int x = r.left(); x <= r.right() + 1; ++x) {
        bool inked = x > r.right();
        for (int y = r.top(); y <= r.bottom() && !inked; ++y) inked = lumAt(img, x, y) < cut;
        if (!inked) { ++run; continue; }
        if (run > best) { best = run; bestFrom = x - run; }
        run = 0;
    }
    if (best <= 0) return qQNaN();
    return (bestFrom + best / 2.0) / scale;
}

QList<std::pair<double, double>> gapsPt(const QImage &img, const QRectF &areaPt, qreal scale, double slope)
{
    // Each run of columns without ink across the area, from where it starts to where it ends.
    // A line lying askew is followed along its slope, so no line beside it counts as its ink.
    QList<std::pair<double, double>> gaps;
    const QRect r = QRectF(areaPt.topLeft() * scale, areaPt.size() * scale).toAlignedRect().intersected(img.rect());
    if (r.width() < 2 || r.height() < 2) return gaps;
    const int paper = backgroundLuminance(img, r);
    int darkest = 255;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x) darkest = qMin(darkest, lumAt(img, x, y));
    if (paper - darkest < 60) return gaps;
    const int cut = (paper + darkest) / 2;
    const int band = qMax(2, r.height() - qCeil(std::abs(slope) * r.width()));
    const auto topAt = [&](int x) { return r.top() + qRound(slope < 0.0 ? -slope * (r.right() - x) : slope * (x - r.left())); };
    // A row of the band inked nearly all along is a rule under or over the line, which is no letter's ink.
    std::vector<int> along(size_t(band), 0);
    for (int x = r.left(); x <= r.right(); ++x)
        for (int k = 0, y = topAt(x); k < band && y + k <= r.bottom(); ++k) along[size_t(k)] += lumAt(img, x, y + k) < cut;
    int ruled = 0;
    for (int &row : along) ruled += row = row >= r.width() * 0.9;
    int run = 0;
    for (int x = r.left(); x <= r.right() + 1; ++x) {
        bool inked = x > r.right();
        const int top = topAt(qMin(x, r.right()));
        int rows = 0;
        for (int y = top; y < top + band && y <= r.bottom() && !inked; ++y) rows += !along[size_t(y - top)] && lumAt(img, x, y) < cut;
        // Ink from the band's top to its bottom is a line of a box, which no letter fills.
        inked = inked || (rows > 0 && rows < (band - ruled) * 0.95);
        if (!inked) { ++run; continue; }
        if (run > 0) gaps.append({ (x - run) / scale, x / scale });
        run = 0;
    }
    return gaps;
}

double slopePt(const QImage &img, const QRectF &areaPt, qreal scale)
{
    const QRect r = QRectF(areaPt.topLeft() * scale, areaPt.size() * scale).toAlignedRect().intersected(img.rect());
    if (r.width() < 8 || r.height() < 4) return 0.0;
    const int paper = backgroundLuminance(img, r);
    int darkest = 255;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x) darkest = qMin(darkest, lumAt(img, x, y));
    if (paper - darkest < 60) return 0.0;
    return slopePx(img, r, (paper + darkest) / 2);
}

}
