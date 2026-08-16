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

// Qt PDF renders onto a TRANSPARENT background — the "paper" reads as
// rgba(0,0,0,0). Composite over white before interpreting any pixel, or
// blank paper counts as pitch black.
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

// Background = most frequent luminance in the region. Taking the mode (not
// "white") keeps the measurement working on colored table rows and on dark
// headers with light text.
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

// ── Font-size calibration ─────────────────────────────────────────────────────
// How tall text LOOKS is not decided by its point size alone: the ink-to-em
// ratio differs from font to font, and the family we paint an edit with is
// rarely the embedded one (the Poppler backend reports no family at all, and
// the vector save substitutes Standard-14 fonts). On top of that, only the
// qpdf scanner knows the real /Tf size — every other source estimates it from
// line boxes. So the size is derived from what is actually on the page: the
// measured ink height of the original glyphs.

// Vertical ink runs inside `region`: a run is a sequence of rows carrying
// pixels that stand out from the region's background, reported as its first
// row, its height in rows, and the number of ink pixels it holds.
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
        // Two pixels per row: a single one is antialiasing noise.
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

// Leftmost column between rows `top` and `bottom` that carries ink, or -1.
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

// The main ink run inside `region` — the run holding the most ink, which for
// text is the body of the letters. `top` is its first row (-1 = nothing found),
// `height` its extent, `left` the first column carrying ink within it.
//
// Both sides of the calibration go through this same function, and that is the
// whole point: page and probe must be measured alike, down to which part of the
// glyphs counts. Taking the densest run rather than the full ink extent is what
// makes that possible — an accent sits beyond a gap that is indistinguishable
// from tight line spacing, so no honest rule could include it on the page side.
// The caller's job is to hand this function a region with room to spare, or the
// run comes back clipped on one side only (see measuredInkPt).
struct InkLine { int top { -1 }; int height { 0 }; int left { -1 }; };

InkLine inkLinePx(const QImage &img, const QRect &region)
{
    const QList<InkRun> runs = inkRuns(img, region);
    if (runs.isEmpty()) return {};

    // Ink count, not height: it keeps accents, rules and cell borders from
    // being mistaken for the text — they are thin AND sparse.
    const InkRun *main = &runs.first();
    for (const InkRun &r : runs)
        if (r.pixels > main->pixels) main = &r;

    InkLine out;
    out.top    = main->top;
    out.height = main->height;
    out.left   = inkLeftPx(img, region, main->top, main->top + main->height - 1);
    return out;
}

}   // namespace

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
    // Nothing dark at all → no real text under the rect; default ink.
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

    // A line box cuts accents and descenders off, so measuring inside it
    // reports LESS ink than the text really has — while the reference this is
    // compared against is drawn with room to spare. That gap alone shrinks
    // every edited line by a few percent. Measuring again with headroom closes
    // it, as long as the extra rows did not drag a neighbouring line in: a
    // second line of text roughly doubles the run, real accents add a fraction.
    const double pad = qMax(1.0, boundsPt.height() * 0.25);
    const MeasuredInk padded = measure(boundsPt.adjusted(0, -pad, 0, pad));
    if (padded.height > tight.height && padded.height <= tight.height * 1.35)
        return padded;
    return tight;
}

FontInk fontInkPerPt(const QString &text, QFont f)
{
    constexpr int kRef = 64;   // large enough that hinting rounding is noise
    f.setPixelSize(kRef);
    const QFontMetricsF fm(f);

    QList<double> heights, rises, bearings;
    const QStringList lines = text.split(u'\n');
    for (const QString &raw : lines) {
        // The vertical extremes come from a handful of glyphs; capping the
        // length keeps the probe image small on very long lines.
        const QString ln = raw.trimmed().left(120);
        if (ln.isEmpty()) continue;

        const int w = qBound(4 * kRef,
                             qCeil(fm.horizontalAdvance(ln)) + 2 * kRef,
                             8000);
        // Pen at (kRef/2, 2*kRef) — drawn from the baseline, so the measured
        // ink box is directly relative to the anchor the caller needs.
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

}   // namespace InkMetrics
