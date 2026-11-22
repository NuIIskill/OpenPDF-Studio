#include "engine/edit/ScanPaper.hpp"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

namespace ScanPaper {

namespace {

// The value below which a share of some values lie.
float shareOf(std::vector<float> values, double share)
{
    if (values.empty()) return 0.0f;
    const size_t at = qMin(values.size() - 1, size_t(double(values.size()) * share));
    std::nth_element(values.begin(), values.begin() + qsizetype(at), values.end());
    return values[at];
}

// The pixels of a scan that hold ink: clearly darker than the paper, with
// the lighter rims that hang on them and a little beyond.
std::vector<uint8_t> inkOf(const Plane &gray, float paper, int reach)
{
    std::vector<float> rest;
    for (const float g : gray.v)
        if (std::abs(g - paper) < 30.0f) rest.push_back(std::abs(g - paper));
    const float noise = 1.4826f * shareOf(rest, 0.5);
    const float core = paper - qMax(24.0f, 6.0f * noise), rim = paper - qMax(8.0f, 2.5f * noise);
    const int w = gray.w, h = gray.h;
    std::vector<uint8_t> ink(gray.v.size(), 0);
    std::deque<int> open;
    for (int i = 0; i < int(gray.v.size()); ++i)
        if (gray.v[size_t(i)] < core) { ink[size_t(i)] = 1; open.push_back(i); }
    while (!open.empty()) {
        const int i = open.front(), x = i % w, y = i / w;
        open.pop_front();
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const size_t n = size_t(ny) * size_t(w) + size_t(nx);
                if (ink[n] || gray.v[n] >= rim) continue;
                ink[n] = 1;
                open.push_back(int(n));
            }
    }
    // Blur and the halo a JPEG leaves around a stroke reach past what is told
    // from the paper's noise, the farther the finer the scan.
    for (int k = 0; k < reach; ++k) {
        const std::vector<uint8_t> was = ink;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t i = size_t(y) * size_t(w) + size_t(x);
                if (was[i]) continue;
                ink[i] = (x > 0 && was[i - 1]) || (x + 1 < w && was[i + 1])
                      || (y > 0 && was[i - size_t(w)]) || (y + 1 < h && was[i + size_t(w)]);
            }
    }
    return ink;
}

}

namespace {

// The ink of an area that is a rule running through it, as a table or a box
// draws: strokes as dark as ink going straight through it from edge to edge,
// which no letter does, as an area ends in the white around its word. Told
// on the strokes themselves, not on the rims and the blur around them.
std::vector<uint8_t> rulesIn(const Plane &gray, float paper, const std::vector<uint8_t> &ink, const QRect &area)
{
    constexpr int kBeyond = 3;
    const int w = gray.w, h = gray.h;
    std::vector<uint8_t> rule(ink.size(), 0);
    const float dark = shareOf(gray.v, 0.02);
    if (paper - dark < 40.0f) return rule;
    // A thin rule prints lighter than the letters' strokes.
    const float cut = paper - (paper - dark) * 0.35f;
    const auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && gray.at(x, y) < cut; };
    const auto mark = [&](int x, int y) {
        if (x >= area.left() && x <= area.right() && y >= area.top() && y <= area.bottom() && ink[size_t(y) * size_t(w) + size_t(x)])
            rule[size_t(y) * size_t(w) + size_t(x)] = 1;
    };
    // From the white above a word to the white below it no letter reaches: a stroke that does is a box's line,
    // also where it ends on the box's edge just beyond.
    for (int x = area.left(); x <= area.right() && area.height() >= 6; ++x) {
        bool through = true;
        for (int y = area.top(); y <= area.bottom() && through; ++y) through = at(x - 1, y) || at(x, y) || at(x + 1, y);
        for (int y = area.top(); y <= area.bottom() && through; ++y)
            for (int dx = -2; dx <= 2; ++dx) mark(x + dx, y);
    }
    if (area.left() - kBeyond >= 0 && area.right() + kBeyond < w)
        for (int y = area.top(); y <= area.bottom(); ++y) {
            bool through = true;
            for (int k = 1; k <= kBeyond && through; ++k) through = at(area.left() - k, y) && at(area.right() + k, y);
            for (int x = area.left(); x <= area.right() && through; ++x) through = at(x, y - 1) || at(x, y) || at(x, y + 1);
            for (int x = area.left(); x <= area.right() && through; ++x)
                for (int dy = -2; dy <= 2; ++dy) mark(x, y + dy);
        }
    return rule;
}

}

Plane grayOf(const QImage &image)
{
    Plane plane;
    if (image.isNull()) return plane;
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    plane.w = rgb.width();
    plane.h = rgb.height();
    plane.v.resize(size_t(plane.w) * size_t(plane.h));
    for (int y = 0; y < plane.h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = 0; x < plane.w; ++x) plane.v[size_t(y) * size_t(plane.w) + size_t(x)] = float(qGray(row[x]));
    }
    return plane;
}

namespace {

// A mean over a run of pixels along rows, then along columns, kept as a running sum.
void runningMean(Plane &plane, int reach)
{
    const int w = plane.w, h = plane.h;
    std::vector<float> line(size_t(qMax(w, h)));
    const auto pass = [&](int count, const auto &at) {
        double sum = 0.0;
        for (int i = -reach; i <= reach; ++i) sum += at(qBound(0, i, count - 1));
        for (int i = 0; i < count; ++i) {
            line[size_t(i)] = float(sum / (2 * reach + 1));
            sum += at(qBound(0, i + reach + 1, count - 1)) - at(qBound(0, i - reach, count - 1));
        }
    };
    for (int y = 0; y < h; ++y) {
        float *row = plane.v.data() + size_t(y) * size_t(w);
        pass(w, [row](int x) { return row[x]; });
        std::copy(line.begin(), line.begin() + w, row);
    }
    for (int x = 0; x < w; ++x) {
        pass(h, [&](int y) { return plane.v[size_t(y) * size_t(w) + size_t(x)]; });
        for (int y = 0; y < h; ++y) plane.v[size_t(y) * size_t(w) + size_t(x)] = line[size_t(y)];
    }
}

}

void blur(Plane &plane, double sigma)
{
    if (sigma < 0.25 || !plane.isValid()) return;
    // A wide blur as three running means, which cost the same however wide.
    if (sigma >= 3.0) {
        const int reach = qMax(1, qRound(sigma * 0.866));
        for (int k = 0; k < 3; ++k) runningMean(plane, reach);
        return;
    }
    const int radius = qCeil(3.0 * sigma);
    std::vector<float> kernel(size_t(2 * radius + 1));
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) sum += kernel[size_t(i + radius)] = float(std::exp(-0.5 * i * i / (sigma * sigma)));
    for (float &k : kernel) k /= sum;
    const int w = plane.w, h = plane.h;
    std::vector<float> other(plane.v.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v = 0.0f;
            for (int i = -radius; i <= radius; ++i) v += kernel[size_t(i + radius)] * plane.at(qBound(0, x + i, w - 1), y);
            other[size_t(y) * size_t(w) + size_t(x)] = v;
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v = 0.0f;
            for (int i = -radius; i <= radius; ++i)
                v += kernel[size_t(i + radius)] * other[size_t(qBound(0, y + i, h - 1)) * size_t(w) + size_t(x)];
            plane.v[size_t(y) * size_t(w) + size_t(x)] = v;
        }
}

bool bilevel(const Plane &gray)
{
    // A scan in black and white holds next to no gray, also where it was drawn a little off its own pixels.
    qsizetype middling = 0, black = 0;
    for (const float g : gray.v) {
        middling += g > 50.0f && g < 205.0f;
        black += g <= 50.0f;
    }
    return black > 0 && middling * 50 < qsizetype(gray.v.size());
}

namespace {

// The mean of the known pixels around each pixel, and how much of them stands there.
struct Around {
    Plane sum[3];
    Plane known;

    float at(int c, size_t i) const { return sum[c].v[i] / known.v[i]; }
};

Around meanOf(const QImage &image, const std::vector<uint8_t> &known, double sigma)
{
    Around out;
    const int w = image.width(), h = image.height();
    out.known.w = w;
    out.known.h = h;
    out.known.v.assign(known.cbegin(), known.cend());
    for (Plane &plane : out.sum) {
        plane.w = w;
        plane.h = h;
        plane.v.assign(size_t(w) * size_t(h), 0.0f);
    }
    for (int y = 0; y < h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            if (!known[i]) continue;
            out.sum[0].v[i] = float(qRed(row[x]));
            out.sum[1].v[i] = float(qGreen(row[x]));
            out.sum[2].v[i] = float(qBlue(row[x]));
        }
    }
    for (Plane &plane : out.sum) blur(plane, sigma);
    blur(out.known, sigma);
    return out;
}

}

namespace {

// The rules found on the page around, given in the page's pixels, marked in a picture that starts at a place of it.
void markRules(std::vector<uint8_t> &rule, int w, int h, const QPoint &at, const QList<QRect> &rules)
{
    for (const QRect &found : rules) {
        const QRect r = (found.width() > found.height() ? found.adjusted(0, -2, 0, 2) : found.adjusted(-2, 0, 2, 0))
                            .translated(-at).intersected(QRect(0, 0, w, h));
        for (int y = r.top(); y <= r.bottom(); ++y)
            for (int x = r.left(); x <= r.right(); ++x) rule[size_t(y) * size_t(w) + size_t(x)] = 1;
    }
}

}

QImage withoutInk(const QImage &around, const QRect &area, int reach, const QPoint &at, uint seed, const QList<QRect> &rules)
{
    // The block around the area's ink takes the paper around it: its tone as
    // the paper nearby has it, smoothly, and its grain from blocks of that
    // paper as the scan's JPEG cut them, so it shows neither a flat patch nor
    // the outlines of what stood there.
    constexpr int kBlock = 8;
    const QImage from = around.convertToFormat(QImage::Format_RGB32);
    const QRect part = area.intersected(from.rect());
    const Plane gray = grayOf(from);
    if (!gray.isValid() || part.isEmpty()) return from.copy(area);
    const int w = gray.w, h = gray.h;
    const auto index = [w](int x, int y) { return size_t(y) * size_t(w) + size_t(x); };
    const float paper = shareOf(gray.v, 0.75);
    const std::vector<uint8_t> ink = inkOf(gray, paper, reach);
    // A rule through the area stays: it is the form's, not the word's.
    std::vector<uint8_t> rule = rulesIn(gray, paper, ink, part);
    markRules(rule, w, h, at, rules);
    QRect fill;
    for (int y = part.top(); y <= part.bottom(); ++y)
        for (int x = part.left(); x <= part.right(); ++x)
            if (ink[index(x, y)] && !rule[index(x, y)]) fill |= QRect(x, y, 1, 1);
    QImage out = from.copy(area);
    if (fill.isEmpty()) return out;
    std::vector<uint8_t> known(ink.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) known[index(x, y)] = !ink[index(x, y)] && !fill.contains(x, y);
    const double near = qMax(1.5, double(reach)), far = qBound(near * 2.0, fill.height() * 0.5, 24.0);
    const Around close = meanOf(from, known, near), wide = meanOf(from, known, far);
    // The paper's usual colour, where no paper stands around at all.
    std::vector<float> all[3];
    for (int y = 0; y < h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(from.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (!known[index(x, y)]) continue;
            all[0].push_back(float(qRed(row[x])));
            all[1].push_back(float(qGreen(row[x])));
            all[2].push_back(float(qBlue(row[x])));
        }
    }
    const float usual[3] = { all[0].empty() ? paper : shareOf(all[0], 0.5), all[1].empty() ? paper : shareOf(all[1], 0.5),
                             all[2].empty() ? paper : shareOf(all[2], 0.5) };
    // Blocks of paper alone, on the grid the scan's blocks lie on.
    const int ox = ((at.x() % kBlock) + kBlock) % kBlock, oy = ((at.y() % kBlock) + kBlock) % kBlock;
    QList<QPoint> blocks;
    for (int by = (kBlock - oy) % kBlock; by + kBlock <= h; by += kBlock)
        for (int bx = (kBlock - ox) % kBlock; bx + kBlock <= w; bx += kBlock) {
            bool clean = true;
            for (int y = by; y < by + kBlock && clean; ++y)
                for (int x = bx; x < bx + kBlock && clean; ++x)
                    clean = known[index(x, y)] && close.known.v[index(x, y)] > 0.5f;
            if (clean) blocks.append(QPoint(bx, by));
        }
    const auto grainAt = [&](int x, int y, int c) {
        const QRgb *row = reinterpret_cast<const QRgb *>(from.constScanLine(y));
        const int value = c == 0 ? qRed(row[x]) : c == 1 ? qGreen(row[x]) : qBlue(row[x]);
        return float(value) - close.at(c, index(x, y));
    };
    for (int y = fill.top(); y <= fill.bottom(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(y - area.top()));
        for (int x = fill.left(); x <= fill.right(); ++x) {
            const size_t i = index(x, y);
            if (rule[i]) continue;
            const float share = qBound(0.0f, close.known.v[i] / 0.3f, 1.0f);
            float value[3];
            for (int c = 0; c < 3; ++c) {
                const float far = wide.known.v[i] > 1e-4f ? wide.at(c, i) : usual[c];
                value[c] = share > 0.0f ? share * close.at(c, i) + (1.0f - share) * far : far;
            }
            if (!blocks.isEmpty()) {
                const uint bx = uint((at.x() + x) >> 3), by = uint((at.y() + y) >> 3);
                const uint pick = (bx * 73856093u) ^ (by * 19349663u) ^ (seed * 83492791u);
                const QPoint block = blocks.at(qsizetype((pick >> 4) % uint(blocks.size())));
                const int gx = block.x() + ((x - block.x()) % kBlock + kBlock) % kBlock;
                const int gy = block.y() + ((y - block.y()) % kBlock + kBlock) % kBlock;
                for (int c = 0; c < 3; ++c) value[c] += grainAt(gx, gy, c);
            }
            row[x - area.left()] = qRgb(qBound(0, qRound(value[0]), 255), qBound(0, qRound(value[1]), 255),
                                        qBound(0, qRound(value[2]), 255));
        }
    }
    return out;
}

namespace {

// A word of the sample: how dark the scan is around it against its paper,
// and where the drawn word lies on it best.
struct Word {
    int index;
    std::vector<float> dark;
    std::vector<uint8_t> near;
    int dx { 0 }, dy { 0 };
    float paper { 255.0f };
};

QList<Word> wordsOf(const QList<Plane> &scans, const QList<QList<Plane>> &drawn, int reach, bool *bilevel)
{
    QList<Word> words;
    qsizetype pixels = 0, middling = 0, black = 0;
    for (int i = 0; i < scans.size() && !drawn.isEmpty(); ++i) {
        const Plane &scan = scans.at(i);
        const Plane &thick = drawn.last().at(i);
        if (!scan.isValid() || scan.w != thick.w || scan.h != thick.h) continue;
        Word word { i, {}, {}, 0, 0 };
        // Close around the word's own strokes, so no ink of a line set tight above or below counts.
        Plane around = thick;
        blur(around, 1.2);
        std::vector<float> rest;
        for (size_t k = 0; k < scan.v.size(); ++k) {
            word.near.push_back(around.v[k] > 0.05f);
            if (!word.near.back()) rest.push_back(scan.v[k]);
            ++pixels;
            middling += scan.v[k] > 50.0f && scan.v[k] < 205.0f;
            black += scan.v[k] <= 50.0f;
        }
        const float paper = rest.size() >= 16 ? shareOf(rest, 0.5) : shareOf(scan.v, 0.8);
        for (const float g : scan.v) word.dark.push_back(paper - g);
        word.paper = paper;
        Plane plain = drawn.at(kPlainGain).at(i);
        blur(plain, 1.0);
        double most = 0.0;
        for (int dy = -reach; dy <= reach; ++dy)
            for (int dx = -reach; dx <= reach; ++dx) {
                double sum = 0.0;
                for (int y = qMax(0, dy); y < qMin(scan.h, scan.h + dy); ++y)
                    for (int x = qMax(0, dx); x < qMin(scan.w, scan.w + dx); ++x)
                        sum += word.dark[size_t(y) * size_t(scan.w) + size_t(x)] * plain.at(x - dx, y - dy);
                if (sum > most) { most = sum; word.dx = dx; word.dy = dy; }
            }
        if (most > 0.0) words.append(word);
    }
    *bilevel = black > 0 && middling * 50 < pixels;
    return words;
}

// Each drawn word blurred, where it lies on the scan best, pixel by pixel around the word.
template <typename Pixel>
void each(const QList<Word> &words, const QList<Plane> &drawn, double sigma, const Pixel &pixel)
{
    for (const Word &word : words) {
        Plane b = drawn.at(word.index);
        blur(b, sigma);
        for (int y = 0; y < b.h; ++y)
            for (int x = 0; x < b.w; ++x) {
                const size_t k = size_t(y) * size_t(b.w) + size_t(x);
                if (!word.near[k]) continue;
                const int sx = x - word.dx, sy = y - word.dy;
                pixel(word.dark[k], sx >= 0 && sy >= 0 && sx < b.w && sy < b.h ? b.at(sx, sy) : 0.0f);
            }
    }
}

// The gain and the cut that give the words as many black pixels as the scan
// has, the cut near the middle and the gain small where several do. Counting
// what differs instead would thin the strokes wherever the face is not quite
// the scan's.
Printed blackAndWhite(const QList<Word> &words, const QList<QList<Plane>> &drawn)
{
    Printed out;
    out.bilevel = true;
    double scanned = 0.0, least = 1e18;
    each(words, drawn.at(kPlainGain), 0.0, [&](float dark, float) { scanned += dark > 100.0f; });
    if (scanned <= 0.0) return out;
    for (int g = 0; g < drawn.size(); ++g)
        for (double cut = 0.3; cut <= 0.801; cut += 0.05) {
            double black = 0.0, both = 0.0;
            each(words, drawn.at(g), 0.8, [&](float dark, float ink) { black += ink > cut; both += ink > cut && dark > 100.0f; });
            const double off = std::abs(black - scanned) / scanned + 0.03 * std::abs(g - kPlainGain) + 0.2 * std::abs(cut - 0.5);
            if (off >= least) continue;
            least = off;
            out.gainPx = kGainsPx[g];
            out.cut = cut;
            out.blurPx = 0.8;
            // Words that share little of their black with the scan were misread or stand elsewhere.
            out.found = both > 0.4 * scanned;
        }
    return out;
}

// How thick and how blurred the scan printed is told badly by its thin strokes alone:
// thinner and more blurred explains its darkness much as thicker and sharper does. Of
// what explains it about as well, the one is taken whose strokes come as dark as the
// scan's own at the scan's ink, the face's own strokes before thickened or thinned ones.
Printed gray(const QList<Word> &words, const QList<QList<Plane>> &drawn)
{
    Printed out;
    double total = 0.0, scanned = 0.0, blackest = 0.0;
    std::vector<float> darks;
    each(words, drawn.at(kPlainGain), 0.0, [&](float dark, float) { total += double(dark) * dark; scanned += dark; darks.push_back(dark); });
    for (const Word &word : words) blackest += word.paper / float(words.size());
    // How dark the strokes are is told at their darkest, at their middle and near their edge: the darkest alone
    // is a few spots where strokes meet or the scan printed heavy.
    constexpr double kShares[] = { 0.98, 0.9, 0.8 };
    double darkAt[3];
    for (int q = 0; q < 3; ++q) darkAt[q] = shareOf(darks, kShares[q]);
    if (total <= 0.0 || scanned <= 0.0 || darkAt[2] <= 0.0) return out;
    constexpr double kBlurs[] = { 0.35, 0.55, 0.75, 1.0, 1.3, 1.7, 2.2, 2.8 };
    constexpr double kDarkCounts = 0.5, kPlainCounts = 0.02;
    double best = -1e9, most = 0.0, shown = 1.0;
    for (int g = 0; g < drawn.size(); ++g)
        for (const double sigma : kBlurs) {
            double both = 0.0, own = 0.0, ink = 0.0;
            std::vector<float> inks;
            each(words, drawn.at(g), sigma, [&](float dark, float drawnInk) {
                both += double(dark) * drawnInk;
                own += double(drawnInk) * drawnInk;
                ink += drawnInk;
                inks.push_back(drawnInk);
            });
            if (own <= 0.0 || both <= 0.0 || ink <= 0.0) continue;
            const double explained = both * both / own / total;
            most = qMax(most, explained);
            // No ink is darker than black on the scan's paper: strokes that would have to be hold less than the scan's.
            const double needed = scanned / ink, contrast = qMin(needed, blackest * 1.08);
            double off = std::log(needed / contrast), middle = 1.0;
            for (int q = 0; q < 3; ++q) {
                const double share = contrast * qMax(0.02, double(shareOf(inks, kShares[q]))) / darkAt[q];
                off += std::abs(std::log(share)) / 3.0;
                if (q == 1) middle = share;
            }
            const double score = explained - kDarkCounts * off - kPlainCounts * std::abs(g - kPlainGain);
            if (score <= best) continue;
            best = score;
            shown = middle;
            out.blurPx = sigma;
            out.gainPx = kGainsPx[g];
            out.contrast = contrast;
        }
    if (most <= 0.5 || out.contrast <= 0.0) return out;
    // Never much lighter or darker than the scan's own strokes: where the face fits them badly, gray letters would
    // stand among black ones.
    out.contrast *= qBound(0.92, shown, 1.08) / shown;
    out.found = true;
    return out;
}

}

namespace {

// The paper's tone along a piece, column by column: a line of text is lit unevenly along its length, hardly over its height.
std::vector<float> paperAlong(const Plane &gray)
{
    const int w = gray.w, h = gray.h, block = qMax(8, h);
    QList<float> tones;
    for (int from = 0; from < w; from += block) {
        std::vector<float> values;
        for (int y = 0; y < h; ++y)
            for (int x = from; x < qMin(w, from + block); ++x) values.push_back(gray.at(x, y));
        tones.append(shareOf(values, 0.75));
    }
    std::vector<float> along(size_t(w), tones.first());
    for (int x = 0; x < w; ++x) {
        const double at = (x + 0.5) / block - 0.5;
        const int a = qBound(0, int(std::floor(at)), int(tones.size()) - 1), b = qMin(a + 1, int(tones.size()) - 1);
        const float share = float(qBound(0.0, at - a, 1.0));
        along[size_t(x)] = tones.at(a) * (1.0f - share) + tones.at(b) * share;
    }
    return along;
}

}

QImage inkAlone(const QImage &piece, const QRect &area, const QPoint &at, const QList<QRect> &rules)
{
    // Each pixel of ink keeps how far it stands from the paper around it
    // towards the piece's ink, in the ink's colour: laid on that paper again
    // it is the pixel it was, and on other paper it takes that paper's tone
    // and grain instead of bringing its own along. A rule running through
    // the piece is the form's and stays behind.
    const QImage from = piece.convertToFormat(QImage::Format_RGB32);
    Plane gray = grayOf(from);
    QImage out(from.size(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    if (!gray.isValid()) return out.copy(area);
    const int w = gray.w, h = gray.h;
    const std::vector<float> along = paperAlong(gray);
    const float paper = shareOf(along, 0.5);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) gray.v[size_t(y) * size_t(w) + size_t(x)] += paper - along[size_t(x)];
    const std::vector<uint8_t> ink = inkOf(gray, paper, 1);
    std::vector<uint8_t> rule = rulesIn(gray, paper, ink, area.intersected(from.rect()));
    markRules(rule, w, h, at, rules);
    std::vector<uint8_t> known(ink.size());
    std::vector<float> depths;
    for (size_t i = 0; i < ink.size(); ++i) {
        known[i] = !ink[i];
        if (ink[i]) depths.push_back(paper - gray.v[i]);
    }
    if (depths.empty()) return out.copy(area);
    const float depth = qMax(30.0f, shareOf(depths, 0.95));
    const Around around = meanOf(from, known, qBound(3.0, h * 0.4, 12.0));
    for (int y = 0; y < h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(from.constScanLine(y));
        QRgb *to = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            if (!ink[i] || rule[i]) continue;
            // The paper here: what stands around, or the tone of its column where ink is all around.
            const bool near = around.known.v[i] > 0.02f;
            const float lift = along[size_t(x)] - paper;
            const float under[3] = { near ? around.at(0, i) : paper + lift, near ? around.at(1, i) : paper + lift,
                                     near ? around.at(2, i) : paper + lift };
            const float tone = near ? (11.0f * under[0] + 16.0f * under[1] + 5.0f * under[2]) / 32.0f : paper + lift;
            const float share = qBound(0.0f, (tone - float(qGray(row[x]))) / depth, 1.0f);
            const int alpha = qRound(share * 255.0f);
            if (alpha <= 0) continue;
            const float value[3] = { float(qRed(row[x])), float(qGreen(row[x])), float(qBlue(row[x])) };
            int colour[3];
            for (int c = 0; c < 3; ++c) colour[c] = qBound(0, qRound(under[c] - (under[c] - value[c]) / share), 255);
            to[x] = qRgba(colour[0], colour[1], colour[2], alpha);
        }
    }
    return out.copy(area);
}

QList<std::pair<int, int>> inkRuns(const QImage &word, int above, int below, const QPoint &at, const QList<QRect> &rules)
{
    // The runs of columns of a word of the scan that hold ink, from left to
    // right: its letters where they stand apart. Ink is what is darker than
    // halfway between the paper and the strokes, so the blur between two
    // letters set close still parts them. A stroke that runs on through the
    // rows above and below the word is a form's line, not a letter's.
    QList<std::pair<int, int>> runs;
    const Plane gray = grayOf(word);
    if (!gray.isValid() || gray.h <= above + below) return runs;
    const float paper = shareOf(gray.v, 0.9), dark = shareOf(gray.v, 0.02);
    if (paper - dark < 40.0f) return runs;
    const float cut = (paper + dark) / 2.0f, faint = paper - (paper - dark) * 0.3f;
    std::vector<uint8_t> rule(size_t(gray.w) * size_t(gray.h), 0);
    markRules(rule, gray.w, gray.h, at, rules);
    for (int x = 0; x < gray.w; ++x) {
        bool inked = false;
        for (int y = above; y < gray.h - below && !inked; ++y) inked = gray.at(x, y) < cut && !rule[size_t(y) * size_t(gray.w) + size_t(x)];
        if (inked && above > 0 && below > 0) {
            const auto faintly = [&](int y) {
                return gray.at(x, y) < faint || (x > 0 && gray.at(x - 1, y) < faint) || (x + 1 < gray.w && gray.at(x + 1, y) < faint);
            };
            int through = 0;
            for (int y = above; y < gray.h - below; ++y) through += faintly(y);
            bool on = through >= (gray.h - above - below) * 0.95;
            for (int k = 1; k <= qMin(3, qMin(above, below)) && on; ++k) on = faintly(above - k) && faintly(gray.h - below - 1 + k);
            inked = !on;
        }
        if (!inked) continue;
        if (!runs.isEmpty() && runs.last().second == x - 1) runs.last().second = x;
        else runs.append({ x, x });
    }
    return runs;
}

Printed printedLike(const QList<Plane> &scans, const QList<QList<Plane>> &drawn, int reach)
{
    bool bilevel = false;
    const QList<Word> words = wordsOf(scans, drawn, reach, &bilevel);
    if (words.isEmpty()) return {};
    return bilevel ? blackAndWhite(words, drawn) : gray(words, drawn);
}

QImage printed(const Plane &ink, const Printed &like, const QColor &fill, const QColor &paper, QRect *inked)
{
    Plane b = ink;
    blur(b, like.bilevel ? 0.8 : like.blurPx);
    // The ink shows as dark against the paper as the scan's own does.
    QColor colour = like.bilevel ? QColor(Qt::black) : fill;
    const double step = qGray(paper.rgb()) - qGray(fill.rgb());
    double strength = like.contrast > 0.0 && step > 8.0 ? like.contrast / step : 1.0;
    if (strength > 1.0 && !like.bilevel) {
        const auto darker = [&](int of, int from) { return qBound(0, qRound(from - (from - of) * strength), 255); };
        colour = QColor(darker(fill.red(), paper.red()), darker(fill.green(), paper.green()), darker(fill.blue(), paper.blue()));
        strength = 1.0;
    }
    QImage out(b.w, b.h, QImage::Format_ARGB32);
    *inked = QRect();
    for (int y = 0; y < b.h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < b.w; ++x) {
            const double v = b.at(x, y);
            const int alpha = like.bilevel ? (v > like.cut ? 255 : 0) : qBound(0, qRound(v * strength * 255.0), 255);
            row[x] = qRgba(colour.red(), colour.green(), colour.blue(), alpha);
            if (alpha > 2) *inked |= QRect(x, y, 1, 1);
        }
    }
    return out;
}

}
