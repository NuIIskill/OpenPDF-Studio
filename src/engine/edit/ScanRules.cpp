#include "engine/edit/ScanRules.hpp"

#include <QtMath>

#include <algorithm>
#include <limits>
#include <vector>

namespace ScanRules {

namespace {

// A stretch of ink along one row or column, its gaps bridged, and the line it was found to be part of.
struct Piece {
    int at { 0 }, from { 0 }, to { 0 };
    int line { 0 };
    int gap { 0 };
};

// The ink of a picture, read along its rows or along its columns.
struct Ink {
    std::vector<uint8_t> on;
    int w { 0 }, h { 0 };
    bool turned { false };

    int across() const { return turned ? h : w; }
    int down() const { return turned ? w : h; }
    bool at(int along, int row) const
    {
        const int x = turned ? row : along, y = turned ? along : row;
        return x >= 0 && y >= 0 && x < w && y < h && on[size_t(y) * size_t(w) + size_t(x)];
    }
    double cover(int row, int from, int to) const
    {
        int n = 0;
        for (int k = from; k <= to; ++k) n += at(k, row);
        return double(n) / double(to - from + 1);
    }
};

// Thin stretches of ink that stand alone: no letters' strokes, which have ink row upon row.
QList<Piece> piecesOf(const Ink &ink, int bridge, int thick, int least)
{
    QList<Piece> out;
    for (int row = 0; row < ink.down(); ++row) {
        int from = -1, last = -1, inked = 0, gap = 0;
        for (int k = 0; k <= ink.across(); ++k) {
            const bool on = k < ink.across() && ink.at(k, row);
            if (on) {
                if (from < 0) { from = k; inked = 0; gap = 0; }
                else gap = qMax(gap, k - last - 1);
                last = k;
                ++inked;
                continue;
            }
            if (from < 0 || (k < ink.across() && k - last <= bridge)) continue;
            const int length = last - from + 1;
            if (length >= least && inked * 4 >= length) {
                // As thick as the rows beside it hold about as much ink along it, and paper beyond those on
                // both sides: the feet of a row of letters have their stems above them.
                const double own = ink.cover(row, from, last);
                int up = 0, down = 0;
                while (up <= thick && row - up - 1 >= 0 && ink.cover(row - up - 1, from, last) >= own * 0.5) ++up;
                while (down <= thick && row + down + 1 < ink.down() && ink.cover(row + down + 1, from, last) >= own * 0.5) ++down;
                const auto paper = [&](int at) { return at < 0 || at >= ink.down() || ink.cover(at, from, last) <= 0.12; };
                if (up + down + 1 <= thick && paper(row - up - 2) && paper(row + down + 2)) out.append({ row, from, last, int(out.size()), gap });
            }
            from = -1;
        }
    }
    // Pieces that go on from row to row are one line, lying a little askew or more than a pixel thick.
    for (bool joined = true; joined;) {
        joined = false;
        for (Piece &a : out)
            for (const Piece &b : std::as_const(out))
                if (b.line < a.line && std::abs(a.at - b.at) <= 1 && a.from <= b.to + bridge && b.from <= a.to + bridge) {
                    a.line = b.line;
                    joined = true;
                }
    }
    return out;
}

// From where to where each line reaches.
QList<std::pair<int, int>> reaches(const QList<Piece> &pieces)
{
    QList<std::pair<int, int>> out(pieces.size(), { std::numeric_limits<int>::max(), -1 });
    for (const Piece &p : pieces) {
        out[p.line].first = qMin(out.at(p.line).first, p.from);
        out[p.line].second = qMax(out.at(p.line).second, p.to);
    }
    return out;
}

}

QList<QRect> find(const QImage &picture, double pxPerPt)
{
    QList<QRect> rules;
    const QImage gray = picture.convertToFormat(QImage::Format_Grayscale8);
    if (gray.isNull() || pxPerPt <= 0.0 || gray.width() < 8 || gray.height() < 4) return rules;
    Ink ink;
    ink.w = gray.width();
    ink.h = gray.height();
    std::vector<int> tones(256, 0);
    for (int y = 0; y < ink.h; ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < ink.w; ++x) ++tones[row[x]];
    }
    const auto share = [&](double part) {
        qint64 seen = 0;
        const qint64 want = qint64(double(ink.w) * ink.h * part);
        for (int t = 0; t < 256; ++t)
            if ((seen += tones[size_t(t)]) > want) return t;
        return 255;
    };
    const int paper = share(0.9);
    // A picture with little on it holds its ink in a far smaller share of its pixels.
    const int dark = paper - share(0.02) >= 40 ? share(0.02) : share(0.0005);
    if (paper - dark < 40) return rules;
    // A thin rule prints lighter than a letter's strokes.
    const int faint = qMax(paper - (paper - dark) * 3 / 10, paper - 45);
    ink.on.resize(size_t(ink.w) * size_t(ink.h));
    for (int y = 0; y < ink.h; ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < ink.w; ++x) ink.on[size_t(y) * size_t(ink.w) + size_t(x)] = row[x] < faint;
    }
    const int bridge = qMax(2, qRound(2.6 * pxPerPt)), thick = qMax(3, qRound(1.4 * pxPerPt));
    // A rule lying three degrees askew stays in one row for a few points only.
    const int least = qMax(6, qRound(2.5 * pxPerPt)), meet = qMax(1, qRound(0.5 * pxPerPt));
    // Lines across are longer than a dash or a word's underline.
    const QList<Piece> across = piecesOf(ink, bridge, thick, least);
    const QList<std::pair<int, int>> wide = reaches(across);
    QList<Piece> lines;
    for (const Piece &p : across)
        if (wide.at(p.line).second - wide.at(p.line).first + 1 >= 24.0 * pxPerPt) lines.append(p);
    for (const Piece &p : std::as_const(lines)) rules.append(QRect(p.from, p.at, p.to - p.from + 1, 1));
    // A line across is no ink beside an upright one that meets it: a low box's side would have none of the paper around it.
    for (const Piece &p : std::as_const(lines))
        std::fill_n(ink.on.begin() + qsizetype(p.at) * ink.w + p.from, p.to - p.from + 1, uint8_t(0));
    // Upright lines look like the stems of letters: they are a form's where they run from one line
    // across to another, as the sides of a box, or far higher than any letter.
    ink.turned = true;
    const QList<Piece> upright = piecesOf(ink, bridge, thick, least);
    const QList<std::pair<int, int>> high = reaches(upright);
    // A line drawn through meets the lines across; a dotted or dashed one ends within one of its own gaps of them.
    const auto meets = [&](int x, int y, int within) {
        for (const Piece &p : std::as_const(lines))
            if (std::abs(p.at - y) <= within && x >= p.from - within && x <= p.to + within) return true;
        return false;
    };
    for (const Piece &p : upright) {
        const auto [top, bottom] = high.at(p.line);
        const int within = meet + p.gap;
        if (bottom - top + 1 >= 40.0 * pxPerPt || (meets(p.at, top, within) && meets(p.at, bottom, within)))
            rules.append(QRect(p.at, p.from, 1, p.to - p.from + 1));
    }
    return rules;
}

}
