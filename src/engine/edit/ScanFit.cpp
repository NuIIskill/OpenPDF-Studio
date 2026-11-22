#include "engine/edit/ScanFit.hpp"

#include <QFontMetricsF>
#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {

using ScanLine::Scan;

constexpr int kRef = 256;
constexpr int kPad = 3;
// The italic is taken only where it covers the scan fairly well: on a line no
// face covers, one fits a little better by chance.
constexpr double kSlantFits = 0.55;

// The darkness of a rendered line, blurred as the scan is.
std::vector<float> blurredInk(const QImage &canvas, double sigma)
{
    const int w = canvas.width(), h = canvas.height();
    std::vector<float> g(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y) {
        const uchar *row = canvas.constScanLine(y);
        for (int x = 0; x < w; ++x) g[size_t(y) * size_t(w) + size_t(x)] = (255 - row[x]) / 255.0f;
    }
    if (sigma > 0.2) {
        // Passes of a three pixel box come close to a Gaussian: each adds a variance of two thirds. A blur
        // between two counts of passes is what the two give, mixed.
        const int r = 1;
        const double wanted = 1.5 * sigma * sigma;
        const int passes = qMax(0, int(std::floor(wanted + 0.02)));
        const float beyond = float(qBound(0.0, wanted - passes, 1.0));
        std::vector<float> t(g.size());
        const auto pass = [&](std::vector<float> &from, std::vector<float> &to, bool across) {
            const int n = across ? w : h, lanes = across ? h : w;
            for (int lane = 0; lane < lanes; ++lane) {
                const auto at = [&](int i) -> float & {
                    const int c = qBound(0, i, n - 1);
                    return from[across ? size_t(lane) * size_t(w) + size_t(c) : size_t(c) * size_t(w) + size_t(lane)];
                };
                float sum = 0.0f;
                for (int i = -r; i <= r; ++i) sum += at(i);
                for (int i = 0; i < n; ++i) {
                    to[across ? size_t(lane) * size_t(w) + size_t(i) : size_t(i) * size_t(w) + size_t(lane)] = sum / float(2 * r + 1);
                    sum += at(i + r + 1) - at(i - r);
                }
            }
        };
        for (int k = 0; k < passes; ++k) { pass(g, t, true); pass(t, g, false); }
        if (beyond > 0.04f) {
            std::vector<float> more = g;
            pass(more, t, true);
            pass(t, more, false);
            for (size_t i = 0; i < g.size(); ++i) g[i] += (more[i] - g[i]) * beyond;
        }
    }
    return g;
}

// How high ink stands, finer than a pixel: its rows darker than the cut, the first and the last counted by
// how far past the cut their darkest pixel lies, or whole where the picture knows but black and white.
template <typename Darkest>
double inkHeight(int from, int to, float cut, bool whole, const Darkest &darkest)
{
    int first = -1, last = -1;
    for (int y = from; y <= to; ++y)
        if (darkest(y) > cut) {
            if (first < 0) first = y;
            last = y;
        }
    if (first < 0) return 0.0;
    const auto edge = [&](int y) { return whole ? 1.0 : qMin(1.0, 2.0 * (darkest(y) - cut) / darkest(y)); };
    return first == last ? edge(first) : last - first - 1 + edge(first) + edge(last);
}

// The runs of ink of a scanned line whose letters stand apart, each with white as wide as a quarter of the
// line's height before the next; none where two of them stand closer.
QList<std::pair<int, int>> runsApart(const Scan &s)
{
    QList<std::pair<int, int>> runs;
    const int least = qMax(2, (s.bottom - s.top + 1) / 4);
    for (int x = s.left, from = -1; x <= s.right + 1; ++x) {
        bool inked = false;
        for (int y = s.top; y <= s.bottom && x <= s.right && !inked; ++y) inked = s.ink.at(x, y);
        if (inked && from < 0) from = x;
        if (inked || from < 0) continue;
        if (!runs.isEmpty() && from - runs.last().second - 1 < least) return {};
        runs.append({ from, x - 1 });
        from = -1;
    }
    return runs;
}

// Whether a scanned line knows but black and white: hardly any of its ink is gray.
bool bilevel(const Scan &s)
{
    qsizetype inked = 0, gray = 0;
    for (int y = s.top; y <= s.bottom; ++y)
        for (int x = s.left; x <= s.right; ++x) {
            const float v = s.dark[size_t(y) * size_t(s.ink.w) + size_t(x)];
            inked += v > 0.1f;
            gray += v > 0.1f && v < 0.9f;
        }
    return inked > 0 && gray * 20 < inked;
}

// How high a scanned line's ink stands, cut halfway between its paper and its ink: all of it, or letter by
// letter where they stand apart and are all as high, as figures are, so a row lying a little askew is no higher.
double scannedHeight(const Scan &s, bool whole, bool byLetter)
{
    const auto highIn = [&](int left, int right) {
        return inkHeight(s.top, s.bottom, 0.5f, whole, [&](int y) {
            const auto row = s.dark.cbegin() + qsizetype(y) * s.ink.w;
            return *std::max_element(row + left, row + right + 1);
        });
    };
    QList<double> highs, others;
    for (int x = s.left, from = -1; byLetter && x <= s.right + 1; ++x) {
        bool inked = false;
        for (int y = s.top; y <= s.bottom && x <= s.right && !inked; ++y) inked = s.dark[size_t(y) * size_t(s.ink.w) + size_t(x)] > 0.5f;
        if (inked && from < 0) from = x;
        if (!inked && from >= 0) {
            const double middle = (from + x - 1) / 2.0;
            const bool foreign = std::any_of(s.foreign.cbegin(), s.foreign.cend(), [middle](const std::pair<int, int> &f) { return middle >= f.first && middle <= f.second; });
            if (const double high = highIn(from, x - 1); high > 0.0) (foreign ? others : highs).append(high);
            from = -1;
        }
    }
    // The scan's own letters tell where three and more of them stand; else those another edit left count too.
    if (highs.size() < 3) highs += others;
    if (highs.size() < 3) return highIn(s.left, s.right);
    std::sort(highs.begin(), highs.end());
    return highs.at(highs.size() / 2);
}

// The darkest fiftieth of a rendered line's rows, as the scan's ink is taken.
float levelOf(const std::vector<float> &ink, int w, int h, const Scan &s)
{
    const int from = qBound(0, kPad + s.top - 2, h - 1), to = qBound(0, kPad + s.bottom + 2, h - 1);
    std::vector<float> band(ink.cbegin() + qsizetype(from) * w, ink.cbegin() + qsizetype(to + 1) * w);
    if (band.empty()) return 0.0f;
    const size_t at = band.size() - 1 - band.size() / 50;
    std::nth_element(band.begin(), band.begin() + qsizetype(at), band.end());
    return band[at];
}

// How high a rendered line shows blurred as the scan is, cut at half its ink as the scan's is.
double shownHeight(const QImage &canvas, double sigma, const Scan &s, bool whole)
{
    const std::vector<float> ink = blurredInk(canvas, whole ? 0.0 : sigma);
    const int w = canvas.width(), h = canvas.height();
    const float level = levelOf(ink, w, h, s);
    if (level <= 0.04f) return 0.0;
    return inkHeight(0, h - 1, level / 2.0f, whole,
                     [&](int y) { return *std::max_element(ink.cbegin() + qsizetype(y) * w, ink.cbegin() + qsizetype(y + 1) * w); });
}

// How soft strokes show: what is darker than a quarter of the ink against what is darker than three quarters.
template <typename Values>
double softOf(const Values &values, float level)
{
    qsizetype faint = 0, full = 0;
    for (const float v : values) {
        faint += v > level * 0.25f;
        full += v > level * 0.75f;
    }
    return full > 0 ? double(faint) / double(full) : 0.0;
}

// How alike the gray of the scan and of the rendered line are, the rendering
// blurred as the scan is: a thin stroke blurred looks as wide as a thick one,
// but not as dark.
double grayCover(const Scan &s, const QImage &canvas, double sigma, double *slope = nullptr,
                 const QList<std::pair<int, int>> &spans = {}, QList<double> *perSpan = nullptr)
{
    const int w = canvas.width();
    const std::vector<float> g = blurredInk(canvas, sigma);
    double best = -1.0;
    // Only the rows of the line and a little around are compared, however tall the sample.
    const int from = qMax(0, s.top - 3), to = qMin(s.ink.h - 1, s.bottom + 3);
    int bestDy = 0;
    for (int dy = -2; dy <= 2; ++dy) {
        double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
        const double n = double(s.ink.w) * double(to - from + 1);
        for (int y = from; y <= to; ++y)
            for (int x = 0; x < s.ink.w; ++x) {
                const double a = s.dark[size_t(y) * size_t(s.ink.w) + size_t(x)];
                const double b = g[size_t(y + kPad + dy) * size_t(w) + size_t(x)];
                sa += a; sb += b; saa += a * a; sbb += b * b; sab += a * b;
            }
        const double va = saa - sa * sa / n, vb = sbb - sb * sb / n;
        if (va <= 0.0 || vb <= 0.0) continue;
        const double r = (sab - sa * sb / n) / std::sqrt(va * vb);
        if (r <= best) continue;
        best = r;
        bestDy = dy;
        // As dark as the scan where the rendering is fully inked: the ink it was printed in.
        if (slope) *slope = (sab - sa * sb / n) / vb;
    }
    if (perSpan)
        for (const auto &[first, last] : spans) {
            double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0, n = 0;
            for (int y = from; y <= to; ++y)
                for (int x = qMax(0, first - 2); x <= qMin(s.ink.w - 1, last + 2); ++x) {
                    const double a = s.dark[size_t(y) * size_t(s.ink.w) + size_t(x)];
                    const double b = g[size_t(y + kPad + bestDy) * size_t(w) + size_t(x)];
                    sa += a; sb += b; saa += a * a; sbb += b * b; sab += a * b; ++n;
                }
            const double va = saa - sa * sa / n, vb = sbb - sb * sb / n;
            perSpan->append(n > 0 && va > 0.0 && vb > 0.0 ? (sab - sa * sb / n) / std::sqrt(va * vb) : -1.0);
        }
    return best;
}

// Each word moved over its own pixels for the best match in gray, as the thin
// strokes of some letters fade where the line's cover cannot tell, and on a
// page lying askew each word stands a little higher or lower: the middle and
// baseline of each, in points.
void placeWords(const Scan &s, const QImage &canvas, const QList<std::pair<int, int>> &spans,
                const std::function<double(int)> &middleOf, double base, int bestDx, int bestDy,
                double blur, qreal scale, ScanFont::Fit &fit)
{
    const std::vector<float> g = blurredInk(canvas, blur);
    const int from = qMax(0, s.top - 2), to = qMin(s.ink.h - 1, s.bottom + 2);
    const int reachWord = qMax(2, qRound((s.bottom - s.top + 1) * 0.12));
    const int reachUp = qMin(kPad, qMax(1, reachWord / 2));
    for (int i = 0; i < spans.size(); ++i) {
        const int left = qMax(0, spans.at(i).first - 2), right = qMin(s.ink.w - 1, spans.at(i).second + 2);
        const auto match = [&](int dx, int dy) {
            double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0, n = 0;
            for (int y = from; y <= to; ++y) {
                const int cy = y + kPad + dy;
                if (cy < 0 || cy >= canvas.height()) continue;
                for (int x = left; x <= right; ++x) {
                    const int cx = x + dx;
                    const double a = s.dark[size_t(y) * size_t(s.ink.w) + size_t(x)];
                    const double b = cx >= 0 && cx < canvas.width() ? g[size_t(cy) * size_t(canvas.width()) + size_t(cx)] : 0.0;
                    sa += a; sb += b; saa += a * a; sbb += b * b; sab += a * b; ++n;
                }
            }
            const double va = saa - sa * sa / n, vb = sbb - sb * sb / n;
            return va > 0.0 && vb > 0.0 ? (sab - sa * sb / n) / std::sqrt(va * vb) : -1.0;
        };
        int bestWord = bestDx, bestUp = bestDy;
        double top = match(bestDx, bestDy);
        for (int dy = bestDy - reachUp; dy <= bestDy + reachUp; ++dy)
            for (int dx = bestDx - reachWord; dx <= bestDx + reachWord; ++dx) {
                const double m = match(dx, dy);
                if (m > top) { top = m; bestWord = dx; bestUp = dy; }
            }
        const auto peak = [&](double before, double after) {
            const double curve = before - 2.0 * top + after;
            return curve < 0.0 ? qBound(-0.5, 0.5 * (before - after) / curve, 0.5) : 0.0;
        };
        const double sub = peak(match(bestWord - 1, bestUp), match(bestWord + 1, bestUp));
        const double subUp = peak(match(bestWord, bestUp - 1), match(bestWord, bestUp + 1));
        const double middle = middleOf(i) - bestWord - sub;
        fit.wordMiddlesPt.append((s.at.x() + middle) / scale);
        fit.wordBaselinesPt.append((s.at.y() + base - kPad - bestUp - subUp + s.drop(middle)) / scale);
    }
}

// How a face at its fitted size covers a scanned line: the size and the share of ink both hold.
// The style of each word against its line's: one printed in the other weight
// carries as much more or less ink as that weight does, beyond what most
// words of the line carry over their rendering; one printed in the other
// slant is matched clearly better in gray by it. The fonts are the line's,
// then the other weight and the other slant.
void wordStyles(const Scan &s, const QStringList &parts, const QList<std::pair<int, int>> &spans,
                const QFont fonts[3], double k, double base, int bestDx, int bestDy, double blur, qreal scale,
                ScanFont::Fit &fit)
{
    const int w = s.ink.w, h = s.ink.h + 2 * kPad;
    const auto drawn = [&](const QFont &f) {
        QImage canvas(w, h, QImage::Format_Grayscale8);
        canvas.fill(255);
        QPainter p(&canvas);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setFont(f);
        p.setPen(Qt::black);
        const QFontMetricsF m(f);
        for (int i = 0; i < parts.size(); ++i) {
            const QRectF t = m.tightBoundingRect(parts.at(i));
            const double middle = (spans.at(i).first + spans.at(i).second + 1) / 2.0;
            p.save();
            p.translate(middle - (t.left() + t.width() / 2.0) * k, base);
            p.scale(k, k);
            p.drawText(QPointF(0, 0), parts.at(i));
            p.restore();
        }
        p.end();
        return blurredInk(canvas, blur);
    };
    const std::vector<float> rendered[3] = { drawn(fonts[0]), drawn(fonts[1]), drawn(fonts[2]) };
    const int from = qMax(0, s.top - 2), to = qMin(s.ink.h - 1, s.bottom + 2);
    struct Measure { double scan { 0.0 }, mass[3] {}, match[3] { -1.0, -1.0, -1.0 }; };
    QList<Measure> measured;
    QList<double> ratios;
    for (int i = 0; i < parts.size(); ++i) {
        Measure m;
        const int left = qMax(0, spans.at(i).first - 2), right = qMin(w - 1, spans.at(i).second + 2);
        for (int f = 0; f < 3; ++f)
            for (int dx = bestDx - 2; dx <= bestDx + 2; ++dx) {
                double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0, n = 0;
                for (int y = from; y <= to; ++y) {
                    const int cy = y + kPad + bestDy;
                    if (cy < 0 || cy >= h) continue;
                    for (int x = left; x <= right; ++x) {
                        const int cx = x + dx;
                        const double a = s.dark[size_t(y) * size_t(w) + size_t(x)];
                        const double b = cx >= 0 && cx < w ? rendered[f][size_t(cy) * size_t(w) + size_t(cx)] : 0.0;
                        sa += a; sb += b; saa += a * a; sbb += b * b; sab += a * b; ++n;
                    }
                }
                const double va = saa - sa * sa / n, vb = sbb - sb * sb / n;
                const double r = va > 0.0 && vb > 0.0 ? (sab - sa * sb / n) / std::sqrt(va * vb) : -1.0;
                if (r > m.match[f]) { m.match[f] = r; m.mass[f] = sb; m.scan = sa; }
            }
        // A figure or a letter alone carries too little ink to tell what most words carry.
        const auto lettered = std::count_if(parts.at(i).cbegin(), parts.at(i).cend(), [](QChar c) { return c.isLetterOrNumber(); });
        if (m.mass[0] > 0.0 && m.scan > 0.0 && lettered >= 2) ratios.append(m.scan / m.mass[0]);
        measured.append(m);
    }
    std::sort(ratios.begin(), ratios.end());
    const bool bold = fonts[0].bold(), italic = fonts[0].italic();
    // Half the words in each weight leave two middles: the one of the line's weight is taken.
    const double ratio = ratios.isEmpty() ? 1.0 : ratios.at(bold ? ratios.size() / 2 : (ratios.size() - 1) / 2);
    for (int i = 0; i < measured.size(); ++i) {
        const Measure &m = measured.at(i);
        const int letters = int(std::count_if(parts.at(i).cbegin(), parts.at(i).cend(), [](QChar c) { return c.isLetterOrNumber(); }));
        const auto off = [&](int f) { return m.mass[f] > 0.0 && m.scan > 0.0 ? std::abs(std::log(m.scan / (ratio * m.mass[f]))) : 1e9; };
        const bool otherWeight = letters >= 2 && ratios.size() >= 3 && off(0) > std::log(1.15) && off(1) < off(0) * 0.6;
        const bool otherSlant = letters >= 3 && m.match[2] > m.match[0] + 0.03 && m.match[2] >= kSlantFits;
        fit.wordBold.append(otherWeight ? !bold : bold);
        fit.wordItalic.append(otherSlant ? !italic : italic);
        // The line's face sets a word of another style off its ink: it goes on the middle of that ink.
        if ((otherWeight || otherSlant) && i < fit.wordMiddlesPt.size())
            fit.wordMiddlesPt[i] = (s.at.x() + (spans.at(i).first + spans.at(i).second + 1) / 2.0) / scale;
    }
}

// Where a rendered line covers the scan's ink best, and how much of both it covers there.
struct Covered {
    double share { -1.0 };
    int dx { 0 }, dy { 0 };
    double subX { 0.0 }, subY { 0.0 };
};

// The rendered line is moved over the scan for the best cover; the final fit
// also sideways, and between pixels by the slope of the cover around it.
Covered coveredAt(const Scan &s, const QImage &canvas, bool fine, const QList<std::pair<int, int>> *spans, QList<double> *perWord)
{
    const int reach = fine ? 2 : 0;
    const auto cover = [&](int dx, int dy, int from, int to) {
        qint64 both = 0, any = 0;
        for (int y = 0; y < s.ink.h; ++y) {
            const uchar *row = canvas.constScanLine(y + kPad + dy);
            for (int x = from; x <= to; ++x) {
                const int rx = x + dx;
                const bool a = s.ink.at(x, y), b = rx >= 0 && rx < s.ink.w && row[rx] < 128;
                both += a && b;
                any += a || b;
            }
        }
        return any > 0 ? double(both) / double(any) : 0.0;
    };
    const auto whole = [&](int dx, int dy) { return cover(dx, dy, 0, s.ink.w - 1); };
    Covered out;
    for (int dy = -kPad; dy <= kPad; ++dy)
        for (int dx = -reach; dx <= reach; ++dx) {
            const double c = whole(dx, dy);
            if (c > out.share) { out.share = c; out.dx = dx; out.dy = dy; }
        }
    if (spans && perWord)
        for (const auto &[first, last] : *spans) perWord->append(cover(out.dx, out.dy, qMax(0, first - 2), qMin(s.ink.w - 1, last + 2)));
    const auto between = [&](double before, double after) {
        const double curve = before - 2.0 * out.share + after;
        return curve < 0.0 ? qBound(-0.5, 0.5 * (before - after) / curve, 0.5) : 0.0;
    };
    if (fine) {
        if (std::abs(out.dy) < kPad) out.subY = between(whole(out.dx, out.dy - 1), whole(out.dx, out.dy + 1));
        out.subX = between(whole(out.dx - 1, out.dy), whole(out.dx + 1, out.dy));
    }
    return out;
}

// Words that would need very different sizes in a face were split wrongly, as the wide letter gaps of a
// fixed pitch face are: the line is taken whole. One word alone off the others is two OCR read as one, or
// misread, and keeps the others apart; so do all words parted where the text layer starts them, and only
// those alike give the size.
QList<int> partedAlike(const Scan &s, const QFontMetricsF &fm, const QString &text, QStringList &parts,
                       QList<std::pair<int, int>> &spans, int *off)
{
    QList<int> unlike;
    if (parts.size() > 1 && s.hinted) {
        QList<double> ratios;
        for (int i = 0; i < parts.size(); ++i)
            ratios.append((spans.at(i).second - spans.at(i).first + 1) / qMax(1.0, fm.tightBoundingRect(parts.at(i)).width()));
        QList<double> sorted = ratios;
        std::sort(sorted.begin(), sorted.end());
        const double usual = sorted.at(sorted.size() / 2);
        for (int i = 0; i < ratios.size(); ++i)
            if (std::abs(std::log(ratios.at(i) / usual)) > std::log(1.25)) unlike.append(i);
    } else if (parts.size() > 1) {
        QList<std::pair<double, int>> ratios;
        for (int i = 0; i < parts.size(); ++i) {
            const double tw = fm.tightBoundingRect(parts.at(i)).width();
            if (tw >= kRef * 0.8) ratios.append({ (spans.at(i).second - spans.at(i).first + 1) / tw, i });
        }
        std::sort(ratios.begin(), ratios.end());
        const auto alike = [](const QList<std::pair<double, int>> &r) {
            return r.isEmpty() || r.last().first <= r.first().first * 1.25;
        };
        if (!alike(ratios)) {
            if (ratios.size() >= 4 && alike(ratios.mid(1))) *off = ratios.first().second;
            else if (ratios.size() >= 4 && alike(ratios.mid(0, ratios.size() - 1))) *off = ratios.last().second;
            else {
                parts = { text.trimmed() };
                spans = { { s.left, s.right } };
            }
        }
    }
    return unlike;
}

}

namespace ScanFit {

ScanFont::Fit fitLine(const Scan &s, const QStringList &words, const QString &text, const QFont &face,
                      qreal scale, bool fine, double stretch, double sigma, double *printed, double blur,
                      bool styles, QList<double> *perWord, double *inked, double *soft)
{
    QFont font = face;
    QFontMetricsF fm(font);
    QStringList parts = s.words.size() == words.size() ? words : QStringList{ text.trimmed() };
    QList<std::pair<int, int>> spans = parts.size() == words.size() ? s.words : QList<std::pair<int, int>>{ { s.left, s.right } };
    int off = -1;
    const QList<int> unlike = partedAlike(s, fm, text, parts, spans, &off);
    QList<QRectF> tight;
    double want = 0.0, have = 0.0, bottom = -1e9;
    QList<std::pair<double, double>> ratios;
    for (int i = 0; i < parts.size(); ++i) {
        tight.append(fm.tightBoundingRect(parts.at(i)));
        bottom = qMax(bottom, tight.last().bottom());
        if (i == off || unlike.contains(i)) continue;
        const double span = spans.at(i).second - spans.at(i).first + 1;
        want += span;
        have += tight.last().width();
        if (tight.last().width() > 0.0) ratios.append({ span / tight.last().width(), span });
    }
    if (have <= 0.0 || want <= 0.0) return {};
    double k = want / have * stretch, spacing = 0.0;
    // Words parted where the text layer starts them give the size most of
    // their width agrees on, so a few in the other weight do not pull it.
    if (s.hinted && ratios.size() >= 3) {
        std::sort(ratios.begin(), ratios.end());
        double seen = 0.0;
        for (const auto &[ratio, span] : std::as_const(ratios))
            if ((seen += span) >= want / 2.0) { k = ratio * stretch; break; }
    }
    // Letters set apart, as in boxed form fields: the width would make the
    // face far taller than the scan, so the height gives the size and the
    // rest of the width is spacing between the letters.
    double top = 1e9;
    for (const QRectF &r : std::as_const(tight)) top = qMin(top, r.top());
    const QString whole = text.trimmed();
    const double height = bottom - top, scanned = s.bottom - s.top + 1, natural = fm.tightBoundingRect(whole).width();
    const bool crisp = bilevel(s);
    const bool level = std::all_of(whole.cbegin(), whole.cend(), [](QChar c) { return c.isSpace() || c.isDigit() || c.isUpper(); });
    // The scan's height is its usual letter's where figures and capitals stand apart: so is the face's, as
    // a face's figures may stand higher than its capitals.
    double usual = height;
    if (level) {
        QList<double> highs;
        for (const QChar c : whole)
            if (!c.isSpace()) highs.append(fm.tightBoundingRect(QString(c)).height());
        std::sort(highs.begin(), highs.end());
        if (highs.size() >= 3) usual = highs.at(highs.size() / 2);
    }
    const auto setApart = [&](double halo, bool measured) {
        k = measured ? (scannedHeight(s, crisp, level) - halo) / usual : scanned / height * stretch;
        spacing = (double(s.right - s.left + 1) / k - natural) / double(whole.size() - 1);
        if (spacing <= 0.0) { spacing = 0.0; return false; }
        font.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
        fm = QFontMetricsF(font);
        parts = { whole };
        spans = { { s.left, s.right } };
        tight = { fm.tightBoundingRect(whole) };
        bottom = tight.first().bottom();
        return true;
    };
    // The letters are set on the scan's baseline; where none was found, the lowest ink meets the lowest letter.
    const auto baseOf = [&] { return s.baseline >= 0 ? kPad + s.baseline + 1.0 : kPad + s.bottom + 1 - bottom * k; };
    double base = 0.0;

    // Each word is set on the middle of its scanned ink, so faded ends on
    // either side weigh alike.
    const auto placed = [&](int i) {
        const double middle = (spans.at(i).first + spans.at(i).second + 1) / 2.0;
        return middle - (tight.at(i).left() + tight.at(i).width() / 2.0) * k;
    };
    const auto painted = [&] {
        QImage canvas(s.ink.w, s.ink.h + 2 * kPad, QImage::Format_Grayscale8);
        canvas.fill(255);
        QPainter p(&canvas);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setFont(font);
        p.setPen(Qt::black);
        for (int i = 0; i < parts.size(); ++i) {
            p.save();
            p.translate(placed(i), base);
            p.scale(k, k);
            p.drawText(QPointF(0, 0), parts.at(i));
            p.restore();
        }
        return canvas;
    };
    // A blurred stroke shows wider than it is drawn: the face's letters stand as high as the scan's where they
    // are blurred and cut as the scan's ink is. So measured the height tells the size alone, also that of
    // letters read one by one, whose widths tell it badly.
    bool byHeight = false;
    const auto sizeByHeight = [&](bool apart) {
        base = baseOf();
        const double shown = shownHeight(painted(), sigma >= 0.0 ? sigma : blur, s, crisp), halo = shown - height * k;
        if (shown <= 0.0 || std::abs(halo) > scanned * 0.15) return;
        if (apart) setApart(halo, true);
        else k = (scannedHeight(s, crisp, level) - halo) / usual;
        byHeight = true;
    };
    // Letters that stand apart show as a run of ink each: where the scan has as many runs as the line has
    // letters, each is set on its own, whatever step lies between two of them, and the usual step tells the
    // spacing a letter typed among them takes.
    QStringList each;
    for (const QChar c : whole)
        if (!c.isSpace()) each.append(QString(c));
    const QList<std::pair<int, int>> runs = each.size() >= 3 ? runsApart(s) : QList<std::pair<int, int>>();
    bool singles = parts.size() >= 3 && std::all_of(parts.cbegin(), parts.cend(), [](const QString &part) { return part.size() == 1; });
    if (runs.size() == each.size() && height > 0.0) {
        parts = each;
        spans = runs;
        tight.clear();
        QList<double> steps;
        double advances = 0.0;
        for (int i = 0; i < parts.size(); ++i) {
            tight.append(fm.tightBoundingRect(parts.at(i)));
            advances += fm.horizontalAdvance(parts.at(i)) / parts.size();
            if (i > 0) steps.append((runs.at(i).first + runs.at(i).second - runs.at(i - 1).first - runs.at(i - 1).second) / 2.0);
        }
        std::sort(steps.begin(), steps.end());
        k = scanned / height;
        sizeByHeight(false);
        spacing = qMax(0.0, steps.at(steps.size() / 2) / k - advances);
        singles = false;
    } else if (whole.size() > 1 && height > 0.0 && height * k > scanned * 1.25 && setApart(0.0, false)) {
        sizeByHeight(true);
    } else if (singles && height > 0.0) {
        k = scanned / height;
        sizeByHeight(false);
    }
    base = baseOf();
    const QImage canvas = painted();
    if (inked) {
        // Counted as the scan's ink is: blurred as the scan, against the darkest fiftieth of the line's rows.
        // A thin stroke blurs to gray, and so counted holds as much as the scan shows of it.
        const std::vector<float> ink = blurredInk(canvas, blur);
        const float level = levelOf(ink, canvas.width(), canvas.height(), s);
        double sum = 0.0;
        for (const float v : ink) sum += v;
        *inked = level > 0.05f ? sum / level : 0.0;
    }
    if (soft) {
        const std::vector<float> ink = blurredInk(canvas, sigma >= 0.0 ? sigma : blur);
        *soft = softOf(ink, levelOf(ink, canvas.width(), canvas.height(), s));
    }
    if (sigma >= 0.0) {
        double slope = 0.0;
        const double gray = grayCover(s, canvas, sigma, printed ? &slope : nullptr, spans,
                                      perWord && parts.size() == words.size() ? perWord : nullptr);
        if (printed) *printed = qBound(0.0, s.paper - slope * (s.paper - s.inkLevel), double(s.paper));
        ScanFont::Fit fit { k * kRef / scale, gray, font.bold(), {}, spacing * k / scale };
        fit.byHeight = byHeight;
        return fit;
    }

    const Covered at = coveredAt(s, canvas, fine, perWord && parts.size() == words.size() ? &spans : nullptr, perWord);
    const double best = at.share, subX = at.subX, subY = at.subY;
    const int bestDx = at.dx, bestDy = at.dy;
    const QPointF origin(s.at.x() + placed(0) - bestDx - subX,
                         s.at.y() + base - kPad - bestDy - subY + s.drop(placed(0) - bestDx - subX));
    ScanFont::Fit fit { k * kRef / scale, best, font.bold(), origin / scale, spacing * k / scale };
    fit.byHeight = byHeight;
    fit.words = parts;
    fit.slope = s.tilt;
    if (s.tilted) fit.slopeReachPt = (s.right - s.left + 1) / scale;
    if (!fine) return fit;
    placeWords(s, canvas, spans, [&](int i) { return placed(i) + (tight.at(i).left() + tight.at(i).width() / 2.0) * k; },
               base, bestDx, bestDy, blur, scale, fit);
    fit.italic = font.italic();
    if (styles && parts.size() > 1 && qFuzzyIsNull(spacing)) {
        QFont fonts[3] = { font, font, font };
        fonts[1].setBold(!font.bold());
        fonts[2].setItalic(!font.italic());
        wordStyles(s, parts, spans, fonts, k, base, bestDx, bestDy, blur, scale, fit);
    }
    return fit;
}

// The blur of a scan, found with the weight that fits best on the line the
// face covers best: a line in another face fits any blur badly. A few figures
// fit several about as well, and tell it by how soft their strokes show.
double blurOf(const QList<Scan> &scans, const QList<QStringList> &words, const QList<ScanFont::Line> &lines,
              const QFont &regular, const QFont &bold, qreal scale, bool bySoftness)
{
    int reference = -1;
    double covered = -1.0;
    for (int i = 0; i < scans.size(); ++i) {
        if (!scans.at(i).isValid()) continue;
        for (const QFont *face : { &regular, &bold }) {
            const double c = fitLine(scans.at(i), words.at(i), lines.at(i).text, *face, scale).score;
            if (c > covered) { covered = c; reference = i; }
        }
    }
    double sigma = 0.82;
    if (reference < 0) return sigma;
    const Scan &s = scans.at(reference);
    const double scanned = bySoftness ? softOf(s.dark, 1.0f) : 0.0;
    constexpr double kTried[] = { 0.0, 0.82, 1.41, 2.16 };
    double top = -1.0;
    for (const QFont *face : { &regular, &bold }) {
        double soft[4] = {}, gray[4] = {};
        int at = 0;
        for (int k = 0; k < 4; ++k) {
            gray[k] = fitLine(s, words.at(reference), lines.at(reference).text, *face, scale, false, 1.0, kTried[k],
                              nullptr, 0.82, false, nullptr, nullptr, scanned > 0.0 ? &soft[k] : nullptr).score;
            if (scanned > 0.0 ? soft[k] > 0.0 && soft[k] <= scanned : gray[k] > gray[at]) at = k;
        }
        if (gray[at] <= top) continue;
        top = gray[at];
        sigma = kTried[at];
        // Between two blurs tried, as far as the scan's strokes show softer than under the sharper one.
        if (scanned > 0.0 && at < 3 && soft[at] > 0.0 && soft[at + 1] > scanned) {
            const double share = (scanned - soft[at]) / (soft[at + 1] - soft[at]);
            sigma = std::sqrt(kTried[at] * kTried[at] + share * (kTried[at + 1] * kTried[at + 1] - kTried[at] * kTried[at]));
        }
    }
    return sigma;
}

Weighed weigh(const Scan &s, const QStringList &words, const QString &text, const QFont &regular,
              const QFont &bold, qreal scale, double sigma)
{
    Weighed w;
    QList<double> wordGray[2], wordCover[2];
    double cover[2] { 0.0, 0.0 };
    const QFont *faces[2] = { &regular, &bold };
    for (int b = 0; b < 2; ++b) {
        const ScanFont::Fit fit = fitLine(s, words, text, *faces[b], scale, false, 1.0, sigma,
                                          nullptr, 0.82, false, &wordGray[b]);
        w.gray[b] = fit.score;
        w.size[b] = fit.sizePt;
        cover[b] = fitLine(s, words, text, *faces[b], scale, false, 1.0, -1.0, nullptr, 0.82, false, &wordCover[b]).score;
    }
    // Most text is set regular: bold has to fit clearly better.
    // The gray hardly sees how thick the strokes are, the ink they cover
    // does; bold covers more of any scan, so it has to cover clearly more.
    // Small print thickens most on a scan, so the smaller the line, the more.
    w.bias = 0.04 * qMax(1.0, 12.0 / qMax(1.0, w.size[0]));
    w.margin = w.gray[1] - w.gray[0] + 0.3 * (cover[1] - cover[0]) - w.bias;
    const qsizetype n = wordGray[0].size();
    if (wordGray[1].size() == n && wordCover[0].size() == n && wordCover[1].size() == n)
        for (qsizetype k = 0; k < n; ++k)
            w.words.append(wordGray[1].at(k) - wordGray[0].at(k) + 0.3 * (wordCover[1].at(k) - wordCover[0].at(k)) - w.bias);
    return w;
}

QFont faceOf(const QString &family, bool bold, bool kerned)
{
    QFont font(family);
    font.setPixelSize(kRef);
    font.setBold(bold);
    font.setKerning(kerned);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::NoFontMerging);
    return font;
}

ScanFont::Fit fitStyled(const Scan &s, const QStringList &words, const QString &text, const QFont &regular,
                        const QFont &bold, qreal scale, double sigma, double nearSize, int weight)
{
    constexpr double kUnclear = 0.15, kAgree = 0.035;
    const Weighed w = weigh(s, words, text, regular, bold, scale, sigma);
    double margin = w.margin;
    bool isBold = margin > 0.0;
    // A line with a few words in the other weight is in the weight most of its words fit.
    const qsizetype parted = s.hinted && words.size() >= 4 ? s.words.size() : 0;
    if (parted && w.words.size() == parted) {
        double heavy = 0.0, light = 0.0;
        for (qsizetype k = 0; k < parted; ++k)
            (w.words.at(k) > 0.0 ? heavy : light) += s.words.at(k).second - s.words.at(k).first + 1;
        // Most text is set regular: bold has to be most of the line clearly.
        isBold = heavy > light * 1.25;
        margin = isBold ? std::abs(margin) : -std::abs(margin);
    }
    // A line found bold only by a little, as small print thickened on a
    // scan, is regular where only that weight gives it the size of the lines nearby.
    const auto agrees = [&](double size) { return size > 0.0 && std::abs(std::log(size / nearSize)) < kAgree; };
    if (isBold && nearSize > 0.0 && margin < kUnclear && agrees(w.size[0]) && !agrees(w.size[1])) {
        isBold = false;
        margin = -margin;
    }
    // The weight told from outside, by the ink of the lines nearby, stands.
    if (weight != 0) {
        isBold = weight > 0;
        margin = isBold ? std::abs(margin) : -std::abs(margin);
    }
    // A line printed slanted is matched clearly better by the italic, and
    // its weight is told again between the italics.
    QFont slanted = isBold ? bold : regular;
    slanted.setItalic(true);
    const double graySlanted = fitLine(s, words, text, slanted, scale, false, 1.0, sigma).score;
    const bool isItalic = graySlanted > w.gray[isBold ? 1 : 0] + 0.03 && graySlanted >= kSlantFits;
    double gray = w.gray[isBold ? 1 : 0];
    if (isItalic) {
        QFont italicRegular = regular, italicBold = bold;
        italicRegular.setItalic(true);
        italicBold.setItalic(true);
        const double italicGrayRegular = fitLine(s, words, text, italicRegular, scale, false, 1.0, sigma).score;
        const double italicGrayBold = fitLine(s, words, text, italicBold, scale, false, 1.0, sigma).score;
        margin = italicGrayBold - italicGrayRegular - w.bias
               + 0.3 * (fitLine(s, words, text, italicBold, scale).score - fitLine(s, words, text, italicRegular, scale).score);
        isBold = margin > 0.0;
        gray = isBold ? italicGrayBold : italicGrayRegular;
    }
    QFont face = isBold ? bold : regular;
    face.setItalic(isItalic);
    // Thin ends of strokes fade on a scan, so the words measure a little
    // short: the size is tried a little larger and smaller for the best gray.
    double stretch = 1.0;
    const auto tryStretch = [&](double tried) {
        const double g = fitLine(s, words, text, face, scale, false, tried, sigma).score;
        if (g > gray) { gray = g; stretch = tried; }
    };
    for (const double step : { -0.03, -0.02, -0.01, 0.01, 0.02, 0.03 }) tryStretch(1.0 + step);
    const double coarse = stretch;
    for (const double step : { -0.005, 0.005 }) tryStretch(coarse + step);
    ScanFont::Fit fit = fitLine(s, words, text, face, scale, true, stretch, -1.0, nullptr, sigma, true);
    fit.boldMargin = margin;
    fitLine(s, words, text, face, scale, false, stretch, sigma, &fit.inkGray);
    // A line no weight covers was misread, a letter too few or too many: its width tells no size, how high its letters stand does.
    constexpr double kMisread = 0.45;
    if (gray < kMisread && s.baseline > s.top && fit.sizePt > 0.0) {
        const double rise = -QFontMetricsF(face).tightBoundingRect(text.trimmed()).top();
        const double high = rise > 0.0 ? (s.baseline - s.top + 1) * kRef / (rise * scale) : 0.0;
        if (high > 0.0 && std::abs(std::log(high / fit.sizePt)) > 0.11) fit.sizePt = high;
    }
    return fit;
}

double roundedSize(double size)
{
    // Documents are set in whole points, some in half points; a size that
    // close is taken as meant, the whole point first, so lines set alike
    // do not fall either side of a half.
    const double whole = qRound(size);
    if (std::abs(whole - size) <= qMin(size * 0.025, 0.4)) return whole;
    const double half = qRound(size * 2.0) / 2.0;
    return std::abs(half - size) <= size * 0.012 ? half : size;
}

}
