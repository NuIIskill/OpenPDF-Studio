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

}
