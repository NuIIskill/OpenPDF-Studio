#include "engine/ocr/OcrMissed.hpp"

#include <QtMath>

#include <algorithm>
#include <vector>

namespace OcrMissed {

namespace {

// A step up to this many of the row's usual is a place left empty in it, a wider one another entry.
constexpr double kHole = 2.2;

// The tone most of a picture has: its paper.
int paperOf(const QImage &gray)
{
    qsizetype tones[256] = {};
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) ++tones[row[x]];
    }
    int paper = 255;
    for (int t = 0, most = 0; t < 256; ++t)
        if (tones[t] > most) { most = int(tones[t]); paper = t; }
    return paper;
}

// The pieces of ink that hang together, each with its box; one that touches what was read already is none.
// Their darkest tones go along, by the place of each in the list.
QList<QRect> piecesOf(const QImage &gray, const std::vector<uint8_t> &taken, int cut, int largest, QList<int> *tones)
{
    const int w = gray.width(), h = gray.height();
    std::vector<uint8_t> seen(size_t(w) * size_t(h), 0);
    const auto ink = [&](int x, int y) { return gray.constScanLine(y)[x] < cut; };
    QList<QRect> out;
    std::vector<int> open;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t at = size_t(y) * size_t(w) + size_t(x);
            if (seen[at] || !ink(x, y)) continue;
            QRect box(x, y, 1, 1);
            bool free = true;
            int darkest = 255;
            open.assign(1, int(at));
            seen[at] = 1;
            while (!open.empty()) {
                const int i = open.back(), px = i % w, py = i / w;
                open.pop_back();
                box |= QRect(px, py, 1, 1);
                darkest = qMin(darkest, int(gray.constScanLine(py)[px]));
                free = free && !taken[size_t(i)];
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = px + dx, ny = py + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        const size_t n = size_t(ny) * size_t(w) + size_t(nx);
                        if (seen[n] || !ink(nx, ny)) continue;
                        seen[n] = 1;
                        open.push_back(int(n));
                    }
            }
            if (free && box.width() <= largest && box.height() <= largest) {
                out.append(box);
                tones->append(darkest);
            }
        }
    return out;
}

// A row goes on over a place left empty, two of its steps wide, and ends where the next letter stands further off.
QList<Row> partedAtFar(const QList<Row> &rows)
{
    QList<Row> parted;
    for (const Row &row : rows) {
        QList<int> steps;
        for (qsizetype i = 1; i < row.letters.size(); ++i) steps.append(row.letters.at(i).left() - row.letters.at(i - 1).left());
        QList<int> sorted = steps;
        std::sort(sorted.begin(), sorted.end());
        const double usual = sorted.isEmpty() ? 0.0 : sorted.at(sorted.size() / 2);
        Row part;
        for (qsizetype i = 0; i < row.letters.size(); ++i) {
            if (i > 0 && steps.at(i - 1) > usual * kHole) {
                parted.append(part);
                part = Row {};
            }
            part.letters.append(row.letters.at(i));
            part.bounds |= row.letters.at(i);
        }
        parted.append(part);
    }
    return parted;
}

}

QList<std::pair<int, int>> Row::words() const
{
    // Letters closer than a quarter of their height are one word's.
    QList<std::pair<int, int>> out { { 0, 0 } };
    for (int i = 1; i < letters.size(); ++i) {
        if (letters.at(i).left() - letters.at(i - 1).right() > bounds.height() / 4) out.append({ i, i });
        else out.last().second = i;
    }
    return out;
}

QList<Row> rows(const QImage &gray, const QList<QRectF> &read, double pxPerPt, QList<int> *upright)
{
    QList<Row> out;
    if (gray.format() != QImage::Format_Grayscale8 || gray.isNull() || pxPerPt <= 0.0) return out;
    const int w = gray.width(), h = gray.height();
    // Letters between a small print's and a heading's height; smaller is a speck or a dot, larger a picture.
    const int least = qMax(4, qRound(3.5 * pxPerPt)), most = qRound(26.0 * pxPerPt), paper = paperOf(gray), cut = paper - 50;
    // A word read whose box holds letters of several rows, one above the other, was read upright
    // through a column of them: it is no word, and its letters are free again.
    std::vector<uint8_t> taken(size_t(w) * size_t(h), 0);
    QList<QRect> all;
    QList<int> tones, darks;
    const QList<QRect> pieces = piecesOf(gray, taken, cut, most, &tones);
    for (qsizetype i = 0; i < pieces.size(); ++i)
        if (pieces.at(i).height() >= least) {
            all.append(pieces.at(i));
            darks.append(paper - tones.at(i));
        }
    for (int i = 0; i < read.size(); ++i) {
        const QRect box = read.at(i).toAlignedRect();
        QList<QRect> inside;
        for (const QRect &piece : std::as_const(all))
            if (box.contains(piece.center())) inside.append(piece);
        std::sort(inside.begin(), inside.end(), [](const QRect &a, const QRect &b) { return a.top() < b.top(); });
        int stacked = inside.isEmpty() ? 0 : 1, below = inside.isEmpty() ? 0 : inside.first().bottom();
        for (const QRect &piece : std::as_const(inside)) {
            if (piece.top() > below) ++stacked;
            below = qMax(below, piece.bottom());
        }
        if (stacked >= 2) {
            if (upright) upright->append(i);
            continue;
        }
        const QRect r = box.adjusted(-2, -2, 2, 2).intersected(gray.rect());
        for (int y = r.top(); y <= r.bottom(); ++y) std::fill_n(taken.begin() + qsizetype(y) * w + r.left(), r.width(), uint8_t(1));
    }
    // A letter joins the row it stands level with and not far behind, where it is as dark as the row's
    // letters: a gray smudge among black figures is none of them.
    const auto rowsOf = [&](bool freeOnly) {
        QList<std::pair<QRect, int>> letters;
        for (qsizetype i = 0; i < all.size(); ++i) {
            const QRect &piece = all.at(i);
            bool free = true;
            for (int y = piece.top(); y <= piece.bottom() && free && freeOnly; ++y)
                for (int x = piece.left(); x <= piece.right() && free; ++x)
                    free = !(taken[size_t(y) * size_t(w) + size_t(x)] && gray.constScanLine(y)[x] < cut);
            if (free) letters.append({ piece, darks.at(i) });
        }
        std::sort(letters.begin(), letters.end(), [](const auto &a, const auto &b) { return a.first.left() < b.first.left(); });
        QList<Row> found;
        QList<int> inked;
        for (const auto &[letter, dark] : std::as_const(letters)) {
            qsizetype joined = -1;
            for (qsizetype r = 0; r < found.size() && joined < 0; ++r) {
                const QRect &last = found.at(r).letters.last();
                const int shared = qMin(last.bottom(), letter.bottom()) - qMax(last.top(), letter.top()) + 1;
                const int high = qMax(last.height(), letter.height());
                if (shared >= high * 0.7 && qMin(last.height(), letter.height()) >= high * 0.7 && letter.left() - last.right() <= high * 7 / 2
                        && qMin(dark, inked.at(r)) >= qMax(dark, inked.at(r)) * 0.45)
                    joined = r;
            }
            if (joined < 0) {
                found.append(Row {});
                inked.append(dark);
                joined = found.size() - 1;
            }
            found[joined].letters.append(letter);
            found[joined].bounds |= letter;
        }
        return partedAtFar(found);
    };
    out = rowsOf(true);
    // Letters far apart are read badly also where they were read, in pieces or with some left out:
    // three and more of them a like step apart are a row to read again, over what was read there.
    for (Row row : rowsOf(false)) {
        if (row.letters.size() < 3) continue;
        QList<int> gaps, steps;
        for (int i = 1; i < row.letters.size(); ++i) {
            gaps.append(row.letters.at(i).left() - row.letters.at(i - 1).right());
            steps.append(row.letters.at(i).left() - row.letters.at(i - 1).left());
        }
        std::sort(gaps.begin(), gaps.end());
        std::sort(steps.begin(), steps.end());
        const bool known = std::any_of(out.cbegin(), out.cend(), [&](const Row &free) { return free.bounds == row.bounds; });
        // All of what stands there: a row with other letters between its own was parted wrongly, as thin
        // figures paler than the rest are.
        const qsizetype there = std::count_if(all.cbegin(), all.cend(), [&](const QRect &piece) { return row.bounds.contains(piece.center()); });
        if (known || there != row.letters.size() || gaps.first() < row.bounds.height() * 0.6 || steps.last() > steps.first() * kHole) continue;
        row.over = true;
        out.append(row);
    }
    out.removeIf([](const Row &row) { return row.letters.size() < 3; });
    return out;
}

QImage closedUp(const QImage &gray, const Row &row)
{
    // The letters set close on the page's paper, as a word: so they are read as the row they are.
    const int high = row.bounds.height(), gap = qMax(3, high / 6), margin = qMax(8, high / 2);
    int wide = 2 * margin + gap * int(row.letters.size() - 1);
    for (const QRect &letter : row.letters) wide += letter.width() + 2;
    QImage out(wide, high + 2 * margin, QImage::Format_Grayscale8);
    out.fill(paperOf(gray));
    int at = margin;
    for (const QRect &letter : row.letters) {
        const QRect from = letter.adjusted(-1, 0, 1, 0).intersected(gray.rect());
        for (int y = 0; y < from.height(); ++y)
            std::copy_n(gray.constScanLine(from.top() + y) + from.left(), from.width(), out.scanLine(margin + from.top() - row.bounds.top() + y) + at);
        at += from.width() + gap;
    }
    return out;
}

}
