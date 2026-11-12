#include "engine/edit/ScanFont.hpp"

#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QStringList>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr int kRef = 256;
constexpr int kPad = 3;

// One family of each metric group that is installed: the faces scans are printed in.
const QStringList &candidates()
{
    static const QStringList families = [] {
        const QList<QStringList> groups = {
            { "Arial", "Liberation Sans", "Arimo", "Nimbus Sans", "Helvetica" },
            { "Times New Roman", "Liberation Serif", "Tinos", "Nimbus Roman", "Times" },
            { "Courier New", "Liberation Mono", "Cousine", "Nimbus Mono PS", "Courier" },
            { "Calibri", "Carlito" },
            { "Cambria", "Caladea" },
            { "Verdana", "DejaVu Sans" },
            { "DejaVu Serif" },
            { "Georgia", "Gelasio" },
            { "Tahoma" },
            { "Segoe UI" },
            { "Trebuchet MS" },
            { "Garamond", "EB Garamond" },
            { "Noto Sans" },
            { "Noto Serif" },
            { "Open Sans" },
        };
        QStringList out;
        for (const QStringList &group : groups)
            for (const QString &family : group)
                if (QFontDatabase::hasFamily(family)) { out.append(family); break; }
        return out;
    }();
    return families;
}

// Faces seldom printed in office documents win only where they fit clearly better.
double rarity(const QString &family)
{
    static const QStringList rare { "Noto Sans", "Noto Serif", "Open Sans" };
    return rare.contains(family) ? 0.015 : 0.0;
}

struct Plane {
    int w { 0 }, h { 0 };
    std::vector<uint8_t> on;
    bool at(int x, int y) const { return on[size_t(y) * size_t(w) + size_t(x)] != 0; }
};

// A scanned line binarized halfway between paper and ink, with the columns its words cover.
struct Scan {
    Plane ink;
    std::vector<float> dark;
    QPoint at;
    int left { 0 }, right { -1 }, top { 0 }, bottom { -1 };
    int baseline { -1 };
    int paper { 255 }, inkLevel { 0 };
    QList<std::pair<int, int>> words;
    bool isValid() const { return right > left && bottom > top; }
};

bool noWord(const QString &word)
{
    return std::none_of(word.cbegin(), word.cend(), [](QChar c) { return c.isLetterOrNumber(); });
}

// A speck of dust apart from an end of the line is no letter, and a word
// without letters or digits OCR read there was read from it.
void dropEndSpecks(Scan &s, QStringList &words)
{
    const int band = s.bottom - s.top + 1;
    const auto inked = [&](int x) {
        for (int y = s.top; y <= s.bottom; ++y)
            if (s.ink.at(x, y)) return true;
        return false;
    };
    for (const bool atEnd : { true, false }) {
        const int step = atEnd ? -1 : 1;
        const int outer = atEnd ? s.right : s.left;
        int inner = outer;
        while (inner + step >= s.left && inner + step <= s.right && inked(inner + step)) inner += step;
        int next = inner + step;
        while (next >= s.left && next <= s.right && !inked(next)) next += step;
        if (next < s.left || next > s.right || std::abs(next - inner) - 1 < qMax(2.0, band * 0.2)) continue;
        int top = s.bottom, bottom = s.top;
        for (int x = qMin(inner, outer); x <= qMax(inner, outer); ++x)
            for (int y = s.top; y <= s.bottom; ++y)
                if (s.ink.at(x, y)) { top = qMin(top, y); bottom = qMax(bottom, y); }
        if (std::abs(outer - inner) + 1 > band * 0.3 || bottom - top + 1 > band * 0.3) continue;
        // Punctuation the text ends in may be the speck, set apart in a fixed pitch face.
        const QString &word = atEnd ? words.last() : words.first();
        const bool read = words.size() > 1 && noWord(word);
        if (!read && !(atEnd ? word.back() : word.front()).isLetterOrNumber()) continue;
        for (int x = qMin(inner, outer); x <= qMax(inner, outer); ++x)
            for (int y = s.top; y <= s.bottom; ++y) {
                s.ink.on[size_t(y) * size_t(s.ink.w) + size_t(x)] = 0;
                s.dark[size_t(y) * size_t(s.ink.w) + size_t(x)] = 0.0f;
            }
        (atEnd ? s.right : s.left) = next;
        if (read) atEnd ? words.removeLast() : words.removeFirst();
    }
}

// The gray of the paper, the most common, and of the ink, the darkest fiftieth, in some rows.
std::pair<int, int> paperAndInk(const std::vector<int> &lum, int width, int from, int to)
{
    int hist[256] = {}, count = 0;
    for (int y = from; y <= to; ++y)
        for (int x = 0; x < width; ++x) { ++hist[lum[size_t(y) * size_t(width) + size_t(x)]]; ++count; }
    int paper = 255, ink = 0;
    for (int g = 0, most = 0; g < 256; ++g)
        if (hist[g] > most) { most = hist[g]; paper = g; }
    for (int g = 0, seen = 0; g < 256; ++g)
        if ((seen += hist[g]) * 50 >= count) { ink = g; break; }
    return { paper, ink };
}

Scan scanOf(const QImage &img, const QRect &px, QStringList &words, int textTop, int textBottom)
{
    Scan s;
    const QRect r = px.intersected(img.rect());
    if (r.width() < 8 || r.height() < 4) return s;
    std::vector<int> lum(size_t(r.width()) * size_t(r.height()));
    for (int y = 0; y < r.height(); ++y)
        for (int x = 0; x < r.width(); ++x)
            lum[size_t(y) * size_t(r.width()) + size_t(x)] = qGray(img.pixel(r.left() + x, r.top() + y));
    auto [bg, dark] = paperAndInk(lum, r.width(), 0, r.height() - 1);
    if (bg - dark < 60) return s;
    const int cut = (bg + dark) / 2;
    s.at = r.topLeft();
    s.ink.w = r.width();
    s.ink.h = r.height();
    s.ink.on.resize(lum.size());
    s.dark.resize(lum.size());
    for (size_t i = 0; i < lum.size(); ++i) s.dark[i] = qBound(0.0f, float(bg - lum[i]) / float(bg - dark), 1.0f);
    s.left = s.ink.w; s.top = s.ink.h;
    for (int y = 0; y < s.ink.h; ++y)
        for (int x = 0; x < s.ink.w; ++x) {
            if (lum[size_t(y) * size_t(s.ink.w) + size_t(x)] >= cut) continue;
            s.ink.on[size_t(y) * size_t(s.ink.w) + size_t(x)] = 1;
            s.left = qMin(s.left, x); s.right = qMax(s.right, x);
            s.top = qMin(s.top, y); s.bottom = qMax(s.bottom, y);
        }
    if (!s.isValid()) return s;

    // The line is the band of ink rows with most ink where its text was
    // found, with the dots and accents just above it; ink of the lines
    // around is dropped.
    std::vector<int> rows(size_t(s.ink.h), 0);
    for (int y = 0; y < s.ink.h; ++y)
        for (int x = 0; x < s.ink.w; ++x) rows[size_t(y)] += s.ink.at(x, y);
    QList<std::pair<int, int>> bands;
    for (int y = 0; y < s.ink.h; ++y) {
        if (!rows[size_t(y)]) continue;
        int e = y;
        while (e + 1 < s.ink.h && rows[size_t(e + 1)]) ++e;
        bands.append({ y, e });
        y = e;
    }
    const auto massOf = [&](const std::pair<int, int> &b) {
        int m = 0;
        for (int y = qMax(b.first, textTop); y <= qMin(b.second, textBottom); ++y) m += rows[size_t(y)];
        return m;
    };
    // Lines set close touch: a band far taller than the text is cut at its thinnest row.
    const int textHeight = textBottom - textTop + 1;
    for (int i = 0; i < bands.size(); ++i) {
        const auto [from, to] = bands.at(i);
        if (to - from + 1 <= textHeight * 1.6) continue;
        int cut = -1;
        for (int y = from + textHeight / 2; y <= to - textHeight / 2; ++y)
            if (cut < 0 || rows[size_t(y)] < rows[size_t(cut)]) cut = y;
        if (cut <= from || cut >= to) continue;
        bands[i] = { from, cut - 1 };
        bands.insert(i + 1, { cut + 1, to });
    }
    int main = 0;
    for (int i = 1; i < bands.size(); ++i)
        if (massOf(bands.at(i)) > massOf(bands.at(main))) main = i;
    const int mainH = bands.at(main).second - bands.at(main).first + 1;
    int top = bands.at(main).first;
    for (int i = main - 1; i >= 0; --i) {
        const auto &b = bands.at(i);
        if (top - b.second - 1 > mainH * 0.35 || b.second - b.first + 1 > mainH * 0.35) break;
        top = b.first;
    }
    // The text's own box holds its accents and descenders: ink beyond it is the lines around.
    const int reach = qMax(2, textHeight / 10);
    s.top = qMax(top, textTop - reach);
    s.bottom = qMin(bands.at(main).second, textBottom + reach);
    if (s.bottom <= s.top) { s.top = top; s.bottom = bands.at(main).second; }
    // Paper and ink are measured again on the line alone: the paper around
    // it would make its ink look darker, its strokes thicker.
    const auto [paper, ink] = paperAndInk(lum, s.ink.w, qMax(0, s.top - 2), qMin(s.ink.h - 1, s.bottom + 2));
    if (paper - ink >= 60) { bg = paper; dark = ink; }
    const int lineCut = (bg + dark) / 2;
    s.paper = bg;
    s.inkLevel = dark;
    s.left = s.ink.w; s.right = -1;
    for (int y = 0; y < s.ink.h; ++y)
        for (int x = 0; x < s.ink.w; ++x) {
            const size_t i = size_t(y) * size_t(s.ink.w) + size_t(x);
            if (y < s.top || y > s.bottom) { s.ink.on[i] = 0; s.dark[i] = 0.0f; continue; }
            s.ink.on[i] = lum[i] < lineCut;
            s.dark[i] = qMax(0.0f, float(bg - lum[i]) / float(bg - dark));
            if (s.ink.on[i]) { s.left = qMin(s.left, x); s.right = qMax(s.right, x); }
        }
    if (!s.isValid()) return s;

    dropEndSpecks(s, words);

    // The letters stand on the last of the rows dense with ink; below it only descenders reach.
    {
        std::vector<int> count(size_t(s.ink.h), 0);
        int most = 0;
        for (int y = s.top; y <= s.bottom; ++y) {
            for (int x = s.left; x <= s.right; ++x) count[size_t(y)] += s.ink.at(x, y);
            most = qMax(most, count[size_t(y)]);
        }
        for (int y = s.bottom; y >= s.top && s.baseline < 0; --y)
            if (count[size_t(y)] >= most * 0.4) s.baseline = y;
    }

    // Words lie apart by the widest gaps between ink columns; when those do
    // not stand out from the gaps inside words, the line is taken whole.
    std::vector<bool> col(size_t(s.ink.w), false);
    for (int x = s.left; x <= s.right; ++x)
        for (int y = s.top; y <= s.bottom && !col[size_t(x)]; ++y) col[size_t(x)] = s.ink.at(x, y);
    QList<std::pair<int, int>> gaps;
    for (int x = s.left; x <= s.right; ++x) {
        if (col[size_t(x)]) continue;
        int e = x;
        while (e + 1 <= s.right && !col[size_t(e + 1)]) ++e;
        gaps.append({ x, e - x + 1 });
        x = e;
    }
    QList<std::pair<int, int>> wide = gaps;
    std::sort(wide.begin(), wide.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    const auto apart = [&](int n) {
        return n >= 2 && wide.size() >= n - 1 && wide.at(n - 2).second >= qMax(2.0, (s.bottom - s.top) * 0.12)
            && (wide.size() == n - 1 || wide.at(n - 2).second >= wide.at(n - 1).second * 1.3);
    };
    // A word without letters or digits at an end, read from a speck beside the line, has no gap of its own.
    while (!apart(int(words.size())) && words.size() > 2 && (noWord(words.last()) || noWord(words.first())))
        noWord(words.last()) ? words.removeLast() : words.removeFirst();
    const int n = int(words.size());
    if (!apart(n)) {
        s.words.append({ s.left, s.right });
        return s;
    }
    wide = wide.mid(0, n - 1);
    std::sort(wide.begin(), wide.end());
    int from = s.left;
    for (const auto &g : std::as_const(wide)) {
        s.words.append({ from, g.first - 1 });
        from = g.first + g.second;
    }
    s.words.append({ from, s.right });
    return s;
}

// How alike the gray of the scan and of the rendered line are, the rendering
// blurred as the scan is: a thin stroke blurred looks as wide as a thick one,
// but not as dark.
double grayCover(const Scan &s, const QImage &canvas, double sigma, double *slope = nullptr)
{
    const int w = canvas.width(), h = canvas.height();
    std::vector<float> g(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y) {
        const uchar *row = canvas.constScanLine(y);
        for (int x = 0; x < w; ++x) g[size_t(y) * size_t(w) + size_t(x)] = (255 - row[x]) / 255.0f;
    }
    if (sigma > 0.4) {
        // Passes of a three pixel box come close to a Gaussian: each adds a variance of two thirds.
        const int r = 1, passes = qMax(1, qRound(1.5 * sigma * sigma));
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
    }
    double best = -1.0;
    // Only the rows of the line and a little around are compared, however tall the sample.
    const int from = qMax(0, s.top - 3), to = qMin(s.ink.h - 1, s.bottom + 3);
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
        // As dark as the scan where the rendering is fully inked: the ink it was printed in.
        if (slope) *slope = (sab - sa * sb / n) / vb;
    }
    return best;
}

// How a face at its fitted size covers a scanned line: the size and the share of ink both hold.
ScanFont::Fit fitLine(const Scan &s, const QStringList &words, const QString &text, const QFont &face,
                      qreal scale, bool fine = false, double stretch = 1.0, double sigma = -1.0,
                      double *printed = nullptr)
{
    QFont font = face;
    QFontMetricsF fm(font);
    QStringList parts = s.words.size() == words.size() ? words : QStringList{ text.trimmed() };
    QList<std::pair<int, int>> spans = parts.size() == words.size() ? s.words : QList<std::pair<int, int>>{ { s.left, s.right } };
    // Words that would need very different sizes in this face were split
    // wrongly, as the wide letter gaps of a fixed pitch face are: the line is taken whole.
    if (parts.size() > 1) {
        double least = 1e9, most = 0.0;
        for (int i = 0; i < parts.size(); ++i) {
            const double tw = fm.tightBoundingRect(parts.at(i)).width();
            if (tw < kRef * 0.8) continue;
            const double ratio = (spans.at(i).second - spans.at(i).first + 1) / tw;
            least = qMin(least, ratio);
            most = qMax(most, ratio);
        }
        if (most > least * 1.25) {
            parts = { text.trimmed() };
            spans = { { s.left, s.right } };
        }
    }
    QList<QRectF> tight;
    double want = 0.0, have = 0.0, bottom = -1e9;
    for (int i = 0; i < parts.size(); ++i) {
        tight.append(fm.tightBoundingRect(parts.at(i)));
        want += spans.at(i).second - spans.at(i).first + 1;
        have += tight.last().width();
        bottom = qMax(bottom, tight.last().bottom());
    }
    if (have <= 0.0 || want <= 0.0) return {};
    double k = want / have * stretch, spacing = 0.0;
    // Letters set apart, as in boxed form fields: the width would make the
    // face far taller than the scan, so the height gives the size and the
    // rest of the width is spacing between the letters.
    double top = 1e9;
    for (const QRectF &r : std::as_const(tight)) top = qMin(top, r.top());
    const QString whole = text.trimmed();
    const double height = bottom - top, scanned = s.bottom - s.top + 1;
    if (whole.size() > 1 && height > 0.0 && height * k > scanned * 1.25) {
        k = scanned / height * stretch;
        const double natural = fm.tightBoundingRect(whole).width();
        spacing = (double(s.right - s.left + 1) / k - natural) / double(whole.size() - 1);
        if (spacing > 0.0) {
            font.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
            fm = QFontMetricsF(font);
            parts = { whole };
            spans = { { s.left, s.right } };
            tight = { fm.tightBoundingRect(whole) };
            bottom = tight.first().bottom();
        } else {
            spacing = 0.0;
        }
    }
    // The letters are set on the scan's baseline; where none was found, the lowest ink meets the lowest letter.
    const double base = s.baseline >= 0 ? kPad + s.baseline + 1.0 : kPad + s.bottom + 1 - bottom * k;

    // Each word is set on the middle of its scanned ink, so faded ends on
    // either side weigh alike.
    const auto placed = [&](int i) {
        const double middle = (spans.at(i).first + spans.at(i).second + 1) / 2.0;
        return middle - (tight.at(i).left() + tight.at(i).width() / 2.0) * k;
    };
    QImage canvas(s.ink.w, s.ink.h + 2 * kPad, QImage::Format_Grayscale8);
    canvas.fill(255);
    {
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
    }
    if (sigma >= 0.0) {
        double slope = 0.0;
        const double gray = grayCover(s, canvas, sigma, printed ? &slope : nullptr);
        if (printed) *printed = qBound(0.0, s.paper - slope * (s.paper - s.inkLevel), double(s.paper));
        return { k * kRef / scale, gray, font.bold(), {}, spacing * k / scale };
    }

    // The rendered line is moved over the scan for the best cover; the final
    // fit also sideways, and between pixels by the slope of the cover around it.
    const int reach = fine ? 2 : 0;
    const auto cover = [&](int dx, int dy) {
        qint64 both = 0, any = 0;
        for (int y = 0; y < s.ink.h; ++y) {
            const uchar *row = canvas.constScanLine(y + kPad + dy);
            for (int x = 0; x < s.ink.w; ++x) {
                const int rx = x + dx;
                const bool a = s.ink.at(x, y), b = rx >= 0 && rx < s.ink.w && row[rx] < 128;
                both += a && b;
                any += a || b;
            }
        }
        return any > 0 ? double(both) / double(any) : 0.0;
    };
    double best = -1.0;
    int bestDx = 0, bestDy = 0;
    for (int dy = -kPad; dy <= kPad; ++dy)
        for (int dx = -reach; dx <= reach; ++dx) {
            const double c = cover(dx, dy);
            if (c > best) { best = c; bestDx = dx; bestDy = dy; }
        }
    const auto between = [&](double before, double after) {
        const double curve = before - 2.0 * best + after;
        return curve < 0.0 ? qBound(-0.5, 0.5 * (before - after) / curve, 0.5) : 0.0;
    };
    double subX = 0.0, subY = 0.0;
    if (fine) {
        if (std::abs(bestDy) < kPad) subY = between(cover(bestDx, bestDy - 1), cover(bestDx, bestDy + 1));
        subX = between(cover(bestDx - 1, bestDy), cover(bestDx + 1, bestDy));
    }
    const QPointF origin(s.at.x() + placed(0) - bestDx - subX,
                         s.at.y() + base - kPad - bestDy - subY);
    return { k * kRef / scale, best, font.bold(), origin / scale, spacing * k / scale };
}

// The blur of a scan, found with the weight that fits best on the line the
// face covers best: a line in another face fits any blur badly.
double blurOf(const QList<Scan> &scans, const QList<QStringList> &words, const QList<ScanFont::Line> &lines,
              const QFont &regular, const QFont &bold, qreal scale)
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
    double top = -1.0;
    for (const double tried : { 0.0, 0.82, 1.41, 2.16 })
        for (const QFont *face : { &regular, &bold }) {
            const double g = fitLine(scans.at(reference), words.at(reference), lines.at(reference).text,
                                     *face, scale, false, 1.0, tried).score;
            if (g > top) { top = g; sigma = tried; }
        }
    return sigma;
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

namespace ScanFont {

Match match(const QImage &img, qreal scale, const QList<Line> &lines, const QList<Line> &context,
            const QString &family)
{
    Match best;
    if (img.isNull() || scale <= 0.0 || lines.isEmpty()) return best;
    QList<Line> all;
    QList<Scan> scans;
    QList<QStringList> words;
    const auto take = [&](const Line &line) {
        all.append(line);
        words.append(line.text.split(u' ', Qt::SkipEmptyParts));
        const QRect px = QRectF(line.inkPt.topLeft() * scale, line.inkPt.size() * scale)
                             .toAlignedRect().adjusted(-2, -2, 2, 2);
        const QRectF text = line.textPt.isEmpty() ? line.inkPt : line.textPt;
        const int top = qFloor(text.top() * scale) - px.top(), bottom = qCeil(text.bottom() * scale) - px.top();
        scans.append(words.last().isEmpty() ? Scan{} : scanOf(img, px, words.last(), top, bottom));
        all.last().text = words.last().join(u' ');
    };
    int chars = 0, height = 0;
    for (const Line &line : lines) {
        take(line);
        chars += int(line.text.trimmed().size());
        if (scans.last().isValid()) height = qMax(height, scans.last().bottom - scans.last().top + 1);
    }
    // A few characters tell faces apart badly: lines nearby whose ink is
    // about as tall help choose.
    for (int i = 0; i < context.size() && chars < 30 && height > 0 && all.size() < lines.size() + 4; ++i) {
        take(context.at(i));
        const Scan &s = scans.last();
        if (!s.isValid() || std::abs(s.bottom - s.top + 1 - height) > height * 0.12) {
            all.removeLast(); words.removeLast(); scans.removeLast();
        }
    }
    const auto faceOf = [](const QString &family, bool bold, bool kerned) {
        QFont font(family);
        font.setPixelSize(kRef);
        font.setBold(bold);
        font.setKerning(kerned);
        font.setHintingPreference(QFont::PreferNoHinting);
        font.setStyleStrategy(QFont::NoFontMerging);
        return font;
    };

    // How well a face covers some lines, compared in gray and a little
    // blurred, so a pixel of offset does not decide.
    const auto grayOf = [&](const QString &name, bool kerned, const QList<int> &on, double contextWeight) {
        double best = -1.0;
        for (const bool bold : { false, true }) {
            const QFont font = faceOf(name, bold, kerned);
            double score = 0.0, weight = 0.0;
            for (const int i : on) {
                double gray = -1.0;
                for (const double sigma : { 0.82, 1.41 })
                    gray = qMax(gray, fitLine(scans.at(i), words.at(i), all.at(i).text, font, scale, false, 1.0, sigma).score);
                const double w = (scans.at(i).right - scans.at(i).left) * (i < lines.size() ? 1.0 : contextWeight);
                if (gray <= -1.0) continue;
                score += gray * w;
                weight += w;
            }
            if (weight > 0.0) best = qMax(best, score / weight);
        }
        return best;
    };
    // Documents are set with kerning or without, as their program does; it
    // is found once, on the face the cheap cover puts first.
    bool kerned = false;
    const auto scoreOf = [&](const QString &name, const QList<int> &on, double contextWeight) {
        return grayOf(name, kerned, on, contextWeight);
    };
    const auto widestOf = [&](int from, int to, int count) {
        QList<int> out;
        for (int i = from; i < to; ++i)
            if (scans.at(i).isValid()) out.append(i);
        std::sort(out.begin(), out.end(), [&](int a, int b) {
            return scans.at(a).right - scans.at(a).left > scans.at(b).right - scans.at(b).left;
        });
        return out.mid(0, count);
    };
    // The face is chosen on the widest of its own lines; when few characters
    // leave the best faces close, the lines nearby decide between those.
    const QList<int> own = widestOf(0, int(lines.size()), 3);
    // The cheap cover sorts the faces first; the gray compares the best few.
    QList<std::pair<double, QString>> rough;
    if (!family.isEmpty()) rough.append({ 1.0, family });
    else
    for (const QString &name : candidates()) {
        double score = -1.0;
        for (const bool bold : { false, true }) {
            const QFont font = faceOf(name, bold, false);
            double sum = 0.0, weight = 0.0;
            for (const int i : own) {
                const Fit fit = fitLine(scans.at(i), words.at(i), all.at(i).text, font, scale);
                if (fit.sizePt <= 0.0) continue;
                const double w = scans.at(i).right - scans.at(i).left;
                sum += fit.score * w;
                weight += w;
            }
            if (weight > 0.0) score = qMax(score, sum / weight);
        }
        if (score > -1.0) rough.append({ score, name });
    }
    std::sort(rough.begin(), rough.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    if (!rough.isEmpty()) kerned = grayOf(rough.first().second, true, own, 1.0) > grayOf(rough.first().second, false, own, 1.0);
    QList<std::pair<double, QString>> ranked;
    if (!family.isEmpty()) ranked.append({ 1.0, family });
    for (int k = 0; family.isEmpty() && k < qMin(4, int(rough.size())); ++k) {
        const double score = scoreOf(rough.at(k).second, own, 1.0);
        if (score > -1.0) ranked.append({ score - rarity(rough.at(k).second), rough.at(k).second });
    }
    if (ranked.isEmpty()) return {};
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    best.family = ranked.first().second;
    if (all.size() > lines.size() && ranked.size() > 1 && ranked.at(0).first - ranked.at(1).first < 0.015) {
        const QList<int> near = own + widestOf(int(lines.size()), int(all.size()), 4);
        double top = -1.0;
        for (int k = 0; k < qMin(3, int(ranked.size())); ++k) {
            if (ranked.at(0).first - ranked.at(k).first >= 0.015) break;
            const double score = scoreOf(ranked.at(k).second, near, 0.5) - rarity(ranked.at(k).second);
            if (score > top) { top = score; best.family = ranked.at(k).second; }
        }
    }
    if (best.family.isEmpty()) return {};
    // Each line in the weight it matches, so a bold line among regular ones shows.
    const QFont regular = faceOf(best.family, false, kerned), bold = faceOf(best.family, true, kerned);
    // The blur is the scan's, kept for all lines.
    const double sigma = blurOf(scans.mid(0, lines.size()), words, all, regular, bold, scale);
    for (int i = 0; i < lines.size(); ++i) {
        if (!scans.at(i).isValid()) { best.lines.append(Fit{}); continue; }
        const Scan &scan = scans.at(i);
        const QString &text = all.at(i).text;
        const double grayRegular = fitLine(scan, words.at(i), text, regular, scale, false, 1.0, sigma).score;
        const double grayBold = fitLine(scan, words.at(i), text, bold, scale, false, 1.0, sigma).score;
        // Most text is set regular: bold has to fit clearly better.
        // The gray hardly sees how thick the strokes are, the ink they cover
        // does; bold covers more of any scan, so it has to cover clearly more.
        const double coverMargin = fitLine(scan, words.at(i), text, bold, scale).score
                                 - fitLine(scan, words.at(i), text, regular, scale).score;
        const double margin = grayBold - grayRegular + 0.3 * coverMargin - 0.04;
        const bool isBold = margin > 0.0;
        const QFont &face = isBold ? bold : regular;
        // Thin ends of strokes fade on a scan, so the words measure a little
        // short: the size is tried a little larger and smaller for the best gray.
        double stretch = 1.0, gray = isBold ? grayBold : grayRegular;
        const auto tryStretch = [&](double tried) {
            const double g = fitLine(scan, words.at(i), text, face, scale, false, tried, sigma).score;
            if (g > gray) { gray = g; stretch = tried; }
        };
        for (const double step : { -0.03, -0.02, -0.01, 0.01, 0.02, 0.03 }) tryStretch(1.0 + step);
        const double coarse = stretch;
        for (const double step : { -0.005, 0.005 }) tryStretch(coarse + step);
        Fit fit = fitLine(scan, words.at(i), text, face, scale, true, stretch);
        fit.boldMargin = margin;
        fitLine(scan, words.at(i), text, face, scale, false, stretch, sigma, &fit.inkGray);
        best.lines.append(fit);
    }
    QList<double> sizes, inks;
    double weight = 0.0;
    for (Fit &fit : best.lines) {
        if (fit.sizePt <= 0.0) continue;
        sizes.append(fit.sizePt = roundedSize(fit.sizePt));
        if (fit.inkGray >= 0.0) inks.append(fit.inkGray);
        weight += fit.boldMargin;
    }
    if (sizes.isEmpty()) return {};
    std::sort(sizes.begin(), sizes.end());
    best.sizePt = sizes.at(sizes.size() / 2);
    std::sort(inks.begin(), inks.end());
    if (!inks.isEmpty()) best.inkGray = inks.at(inks.size() / 2);
    best.bold = weight > 0.0;
    return best;
}

}
