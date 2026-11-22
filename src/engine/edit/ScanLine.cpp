#include "engine/edit/ScanLine.hpp"

#include "engine/edit/InkMetrics.hpp"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace ScanLine {

bool noWord(const QString &word)
{
    return std::none_of(word.cbegin(), word.cend(), [](QChar c) { return c.isLetterOrNumber(); });
}

namespace {

// A speck of dust apart from an end of the line is no letter, and a word
// without letters or digits OCR read there was read from it.
void dropEndSpecks(Scan &s, QStringList &words, QList<double> &starts)
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
        if (read && starts.size() == words.size()) atEnd ? starts.removeLast() : starts.removeFirst();
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

// The rows of a line sheared by its tilt, so the line runs level.
void levelLine(const Scan &s, std::vector<int> &lum, int w, int h, int paper)
{
    std::vector<int> level(lum.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double from = y + s.drop(x);
            const int y0 = qFloor(from);
            const double f = from - y0;
            const auto at = [&](int row) { return row >= 0 && row < h ? lum[size_t(row) * size_t(w) + size_t(x)] : paper; };
            level[size_t(y) * size_t(w) + size_t(x)] = qRound(at(y0) * (1.0 - f) + at(y0 + 1) * f);
        }
    lum.swap(level);
}

// The words of a line parted by the gaps ending where the text layer starts
// each word; a start with no gap between words near it lies inside a word OCR
// read as two, or before a mark it read apart, which are taken together.
// False where many starts lie so.
bool partAtStarts(Scan &s, const QList<std::pair<int, int>> &gaps, const QList<double> &starts, QStringList &words)
{
    const int band = s.bottom - s.top + 1;
    const double least = qMax(2.0, band * 0.08), close = qMax(3.0, band * 0.3);
    // The gap as wide as asked whose end lies nearest a start within reach, after the one taken before.
    const auto nearestGap = [&](double at, int after, double reach, double wide) {
        int pick = -1;
        double nearest = reach;
        for (int g = 0; g < gaps.size(); ++g) {
            const auto &gap = gaps.at(g);
            if (gap.first <= after || gap.second < wide) continue;
            const double d = std::abs(gap.first + gap.second - at);
            if (d < nearest) { nearest = d; pick = g; }
        }
        return pick;
    };
    // Gaps between letters can be clear too: the gaps at most starts tell how wide one between words is.
    QList<int> widths;
    for (qsizetype j = 1, after = s.left; j < starts.size(); ++j)
        if (const int g = nearestGap(starts.at(j) - s.at.x(), int(after), close, least); g >= 0) {
            widths.append(gaps.at(g).second);
            after = gaps.at(g).first + gaps.at(g).second;
        }
    std::sort(widths.begin(), widths.end());
    const double wide = widths.size() >= 3 ? qMax(least, widths.at(widths.size() / 2) * 0.5) : least;
    QList<std::pair<int, int>> chosen;
    QStringList joined { words.first() };
    int missed = 0;
    int after = s.left;
    double shift = 0.0;
    for (qsizetype j = 1; j < starts.size(); ++j) {
        const double at = starts.at(j) - s.at.x();
        // OCR may put a start a little off its gap: a gap a bit further away is taken next.
        int pick = nearestGap(at, after, close, wide);
        if (pick < 0) pick = nearestGap(at, after, band * 0.6, wide);
        // OCR may place a run of words off by about the same: the word before's offset is tried too.
        if (pick < 0) pick = nearestGap(at + shift, after, close, wide);
        if (pick < 0) {
            joined.last() += words.at(j);
            ++missed;
            continue;
        }
        chosen.append(gaps.at(pick));
        joined.append(words.at(j));
        after = gaps.at(pick).first + gaps.at(pick).second;
        shift = after - at;
    }
    // One word of three or more read as two still leaves the others telling where the words stand.
    if (missed > (starts.size() >= 3 ? qMax(qsizetype(1), starts.size() / 4) : 0)) return false;
    words = joined;
    int from = s.left;
    for (const auto &g : std::as_const(chosen)) {
        s.words.append({ from, g.first - 1 });
        from = g.first + g.second;
    }
    s.words.append({ from, s.right });
    s.hinted = true;
    return true;
}

}

// The rows a line's own ink stands in, from the top of its accents to its lowest row. The
// baseline is where the text layer has the line stand in the levelled picture, where it tells.
static std::pair<int, int> ownRows(const Scan &s, int textTop, int textBottom, double base)
{
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
    // A sloping line, levelled about its middle, keeps the middle of the
    // upright box it stood in, while its neighbours reach into the box's ends:
    // of the bands with much ink there, it is the one nearest that middle.
    if (s.tilt != 0.0) {
        const double middle = (textTop + textBottom) / 2.0;
        const auto off = [&](int i) { return std::abs((bands.at(i).first + bands.at(i).second) / 2.0 - middle); };
        const int most = massOf(bands.at(main));
        for (int i = 0; i < bands.size(); ++i)
            if (massOf(bands.at(i)) >= most * 0.4 && off(i) < off(main)) main = i;
        // Where the text layer tells the baseline the line starts on, the band standing
        // on it is the line's: its box may hold the line beside it as well.
        if (base >= 0.0) {
            const int high = bands.at(main).second - bands.at(main).first + 1;
            const auto on = [&](int i) {
                return qMin(double(bands.at(i).second), base + high * 0.3) - qMax(double(bands.at(i).first), base - high);
            };
            for (int i = 0; i < bands.size(); ++i)
                if (massOf(bands.at(i)) >= most * 0.4 && on(i) > on(main)) main = i;
        }
    }
    const int mainH = bands.at(main).second - bands.at(main).first + 1;
    int top = bands.at(main).first;
    for (int i = main - 1; i >= 0; --i) {
        const auto &b = bands.at(i);
        if (top - b.second - 1 > mainH * 0.35 || b.second - b.first + 1 > mainH * 0.35) break;
        top = b.first;
    }
    return { top, bands.at(main).second };
}

// A line of a box beside a letter, as of a field with a figure in each box, runs on above and
// below the line's own rows, where no letter reaches: it is none of the line's ink.
static void dropBoxLines(Scan &s, const std::vector<int> &lum, int cut)
{
    const int beyond = qMax(2, (s.bottom - s.top + 1) / 4), from = qMax(0, s.top - beyond), to = qMin(s.ink.h - 1, s.bottom + beyond);
    if (!s.isValid() || to - from < (s.bottom - s.top) + beyond) return;
    bool any = false;
    for (int x = 0; x < s.ink.w; ++x) {
        int inked = 0;
        for (int y = from; y <= to; ++y) inked += lum[size_t(y) * size_t(s.ink.w) + size_t(x)] < cut;
        if (inked < (to - from + 1) * 0.9) continue;
        any = true;
        for (int y = 0; y < s.ink.h; ++y) {
            s.ink.on[size_t(y) * size_t(s.ink.w) + size_t(x)] = 0;
            s.dark[size_t(y) * size_t(s.ink.w) + size_t(x)] = 0.0f;
        }
    }
    if (!any) return;
    s.left = s.ink.w;
    s.right = -1;
    for (int y = s.top; y <= s.bottom; ++y)
        for (int x = 0; x < s.ink.w; ++x)
            if (s.ink.at(x, y)) { s.left = qMin(s.left, x); s.right = qMax(s.right, x); }
}

// The letters stand on the last of the rows dense with ink; below it only descenders reach.
static void findBaseline(Scan &s)
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

static void partWords(Scan &s, QStringList &words, const QList<double> &starts)
{
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
    if (starts.size() == words.size() && words.size() >= 2 && partAtStarts(s, gaps, starts, words)) return;
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
        return;
    }
    wide = wide.mid(0, n - 1);
    std::sort(wide.begin(), wide.end());
    int from = s.left;
    for (const auto &g : std::as_const(wide)) {
        s.words.append({ from, g.first - 1 });
        from = g.first + g.second;
    }
    s.words.append({ from, s.right });
}

Scan scanOf(const QImage &img, const QRect &px, QStringList &words, int textTop, int textBottom,
            QList<double> starts, double baseRow, double tilt)
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
    int cut = (bg + dark) / 2;
    // A line long enough to tell its slope is sheared level first, so its rows
    // are its own and no neighbour's; columns stay where they are. A short one
    // on a page known to lie clearly askew takes the page's slope.
    const bool telling = r.width() >= 8 * qMax(1, textBottom - textTop);
    if (telling || std::abs(tilt) >= 0.008) {
        s.tilted = true;
        s.tiltAt = r.width() / 2.0;
        if (const double t = telling ? InkMetrics::slopePx(img, r, cut) : tilt; std::abs(t) >= 0.0004) {
            s.tilt = t;
            levelLine(s, lum, r.width(), r.height(), bg);
            std::tie(bg, dark) = paperAndInk(lum, r.width(), 0, r.height() - 1);
            if (bg - dark < 60) return s;
            cut = (bg + dark) / 2;
        }
    }
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

    const double start = starts.isEmpty() ? 0.0 : starts.first() - r.left();
    const auto [top, bottom] = ownRows(s, textTop, textBottom, baseRow >= 0.0 ? baseRow - s.drop(start) : -1.0);
    const int textHeight = textBottom - textTop + 1;
    // The text's own box holds its accents and descenders: ink beyond it is the lines around.
    // A sloping line's box may stand where it starts, off its middle by the slope across half the line.
    const int reach = qMax(2, textHeight / 10) + (s.tilt != 0.0 ? qCeil(std::abs(s.tilt) * s.tiltAt) : 0);
    s.top = qMax(top, textTop - reach);
    s.bottom = qMin(bottom, textBottom + reach);
    if (s.bottom <= s.top) { s.top = top; s.bottom = bottom; }
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
    dropBoxLines(s, lum, lineCut);
    if (!s.isValid()) return s;

    dropEndSpecks(s, words, starts);

    findBaseline(s);
    partWords(s, words, starts);
    return s;
}

}
