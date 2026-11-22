#include "engine/edit/ScanPen.hpp"

#include <QList>
#include <QRect>
#include <QtMath>

#include <algorithm>
#include <vector>

namespace ScanPen {

namespace {

// Ink is clearly darker than the paper, coloured where its red, green and blue lie far apart, plain where close.
constexpr int kInk = 30, kColoured = 26, kPlain = 18;

int chromaOf(QRgb c)
{
    return qMax(qRed(c), qMax(qGreen(c), qBlue(c))) - qMin(qRed(c), qMin(qGreen(c), qBlue(c)));
}

}

namespace {

// A piece of ink that hangs together: its box and its pixels.
struct Piece {
    QRect box;
    std::vector<int> pixels;
};

QList<Piece> piecesOf(const QImage &gray, int cut)
{
    QList<Piece> out;
    const int w = gray.width(), h = gray.height();
    std::vector<uint8_t> seen(size_t(w) * size_t(h), 0);
    std::vector<int> open;
    for (int y0 = 0; y0 < h; ++y0)
        for (int x0 = 0; x0 < w; ++x0) {
            const size_t start = size_t(y0) * size_t(w) + size_t(x0);
            if (seen[start] || gray.constScanLine(y0)[x0] >= cut) continue;
            Piece piece { QRect(x0, y0, 1, 1), {} };
            open.assign(1, int(start));
            seen[start] = 1;
            while (!open.empty()) {
                const int i = open.back(), x = i % w, y = i / w;
                open.pop_back();
                piece.pixels.push_back(i);
                piece.box |= QRect(x, y, 1, 1);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        const size_t k = size_t(ny) * size_t(w) + size_t(nx);
                        if (seen[k] || gray.constScanLine(ny)[nx] >= cut) continue;
                        seen[k] = 1;
                        open.push_back(int(k));
                    }
            }
            out.append(piece);
        }
    return out;
}

// Whether a piece is one curve: hardly any of its rows or columns crosses it more than three times, as
// those of a bracket, a ring or a tick do; letters hanging on it are crossed more often.
bool oneCurve(const Piece &piece, int w)
{
    const QRect &b = piece.box;
    std::vector<uint8_t> on(size_t(b.width()) * size_t(b.height()), 0);
    for (const int i : piece.pixels) on[size_t(i / w - b.top()) * size_t(b.width()) + size_t(i % w - b.left())] = 1;
    const auto crossed = [&](bool level) {
        const int lanes = level ? b.height() : b.width(), n = level ? b.width() : b.height();
        std::vector<int> runs;
        for (int lane = 0; lane < lanes; ++lane) {
            int count = 0;
            for (int i = 0; i < n; ++i) {
                const bool here = on[level ? size_t(lane) * size_t(b.width()) + size_t(i) : size_t(i) * size_t(b.width()) + size_t(lane)];
                const bool before = i > 0 && on[level ? size_t(lane) * size_t(b.width()) + size_t(i - 1) : size_t(i - 1) * size_t(b.width()) + size_t(lane)];
                count += here && !before;
            }
            runs.push_back(count);
        }
        std::sort(runs.begin(), runs.end());
        return runs[runs.size() * 9 / 10];
    };
    return crossed(true) <= 3 && crossed(false) <= 3;
}

}

QImage withoutCurves(const QImage &gray, double pxPerPt)
{
    // A stroke drawn by hand beside the print, a bracket, a ring around a figure: one long thin curve with no
    // piece as high standing level beside it, as a large letter of a heading has its neighbours.
    if (gray.format() != QImage::Format_Grayscale8 || pxPerPt <= 0.0) return gray;
    const int w = gray.width(), h = gray.height();
    qsizetype tones[256] = {};
    for (int y = 0; y < h; ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < w; ++x) ++tones[row[x]];
    }
    int paper = 255;
    for (int t = 0; t < 256; ++t)
        if (tones[t] > tones[paper]) paper = t;
    const QList<Piece> pieces = piecesOf(gray, paper - 50);
    const int high = qRound(pxPerPt * 18.0), wide = qRound(pxPerPt * 30.0), tall = qRound(pxPerPt * 10.0);
    QImage out = gray;
    for (const Piece &piece : pieces) {
        const QRect &b = piece.box;
        if ((b.height() <= high && b.width() <= wide) || double(piece.pixels.size()) >= double(b.width()) * b.height() * 0.15) continue;
        const bool beside = std::any_of(pieces.cbegin(), pieces.cend(), [&](const Piece &other) {
            const QRect &o = other.box;
            if (&other == &piece || o.height() < tall || o.height() * 10 < b.height() * 7 || o.height() * 7 > b.height() * 10) return false;
            const int shared = qMin(o.bottom(), b.bottom()) - qMax(o.top(), b.top()) + 1;
            const int apart = qMax(o.left() - b.right(), b.left() - o.right());
            return shared * 10 >= qMin(o.height(), b.height()) * 7 && apart <= b.height() * 3 / 2;
        });
        if (beside || !oneCurve(piece, w)) continue;
        // A form's rule the stroke crosses hangs together with it and stays: the rows and columns the piece fills along half its box.
        std::vector<int> across(size_t(b.height()), 0), down(size_t(b.width()), 0);
        for (const int i : piece.pixels) {
            ++across[size_t(i / w - b.top())];
            ++down[size_t(i % w - b.left())];
        }
        const auto ruled = [&](int i) {
            const int row = i / w - b.top(), column = i % w - b.left();
            for (int k = -2; k <= 2; ++k)
                if ((row + k >= 0 && row + k < b.height() && b.width() > wide && across[size_t(row + k)] * 2 >= b.width())
                        || (column + k >= 0 && column + k < b.width() && b.height() > wide && down[size_t(column + k)] * 2 >= b.height()))
                    return true;
            return false;
        };
        for (const int i : piece.pixels) {
            if (ruled(i)) continue;
            for (int y = qMax(0, i / w - 1); y <= qMin(h - 1, i / w + 1); ++y)
                for (int x = qMax(0, i % w - 1); x <= qMin(w - 1, i % w + 1); ++x) out.scanLine(y)[x] = uchar(paper);
        }
    }
    return out;
}

QImage without(const QImage &picture, double pxPerPt)
{
    if (picture.isNull() || pxPerPt <= 0.0 || picture.format() == QImage::Format_Grayscale8) return picture;
    QImage out = picture.convertToFormat(QImage::Format_RGB32);
    const int w = out.width(), h = out.height();
    qsizetype tones[256] = {};
    for (int y = 0; y < h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(out.constScanLine(y));
        for (int x = 0; x < w; ++x) ++tones[qGray(row[x])];
    }
    int paper = 255;
    for (int t = 0; t < 256; ++t)
        if (tones[t] > tones[paper]) paper = t;
    std::vector<uint8_t> coloured(size_t(w) * size_t(h), 0);
    qsizetype inked = 0, plain = 0, pen = 0;
    for (int y = 0; y < h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(out.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (paper - qGray(row[x]) <= kInk) continue;
            ++inked;
            const int chroma = chromaOf(row[x]);
            plain += chroma < kPlain;
            if (chroma >= kColoured) { coloured[size_t(y) * size_t(w) + size_t(x)] = 1; ++pen; }
        }
    }
    // A page printed in colour has no pen to tell from its print.
    if (pen == 0 || plain * 2 < inked) return picture;
    // A pen's stroke runs far and fills little of the room it runs through: a coloured letter, however large,
    // fills more of its own, and a small one may be print as well.
    const int high = qRound(pxPerPt * 18.0), wide = qRound(pxPerPt * 30.0), fringe = qMax(1, qRound(pxPerPt * 0.3));
    const QRgb blank = qRgb(paper, paper, paper);
    std::vector<int> open, piece;
    for (int y0 = 0; y0 < h; ++y0)
        for (int x0 = 0; x0 < w; ++x0) {
            const size_t start = size_t(y0) * size_t(w) + size_t(x0);
            if (coloured[start] != 1) continue;
            QRect box(x0, y0, 1, 1);
            open.assign(1, int(start));
            piece.clear();
            coloured[start] = 2;
            while (!open.empty()) {
                const int i = open.back(), x = i % w, y = i / w;
                open.pop_back();
                piece.push_back(i);
                box |= QRect(x, y, 1, 1);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        const size_t k = size_t(ny) * size_t(w) + size_t(nx);
                        if (coloured[k] != 1) continue;
                        coloured[k] = 2;
                        open.push_back(int(k));
                    }
            }
            if ((box.height() <= high && box.width() <= wide) || double(piece.size()) >= double(box.width()) * box.height() * 0.15) continue;
            // The stroke leaves with the fringe around it, but for the print's own ink where it crosses a letter.
            for (const int i : piece)
                for (int y = qMax(0, i / w - fringe); y <= qMin(h - 1, i / w + fringe); ++y) {
                    QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(y));
                    for (int x = qMax(0, i % w - fringe); x <= qMin(w - 1, i % w + fringe); ++x)
                        if (paper - qGray(row[x]) <= kInk || chromaOf(row[x]) >= kPlain) row[x] = blank;
                }
        }
    return out;
}

}
