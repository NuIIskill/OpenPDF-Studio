#include "ui/edit/EditOpen.hpp"

#ifdef HAVE_PDF_RENDERING

#include <QtMath>

namespace {

// The ink of a line's picture that is its letters': without the rules of a form, a box's long
// lines across and the lines that run from one of those to the other, or through the whole height.
QList<QList<bool>> lettersInk(const QImage &line, int top, int bottom, int inkPx, int cut, int faint, QList<QRect> *rules)
{
    const int w = line.width(), h = bottom - top + 1, middle = h / 2;
    QList<QList<bool>> across(h, QList<bool>(w, false)), ink(h, QList<bool>(w, false));
    struct Piece { int y, from, to, line; };
    QList<Piece> pieces;
    for (int y = 0; y < h; ++y)
        for (int x = 0, from = -1; x <= w; ++x) {
            const bool on = x < w && qGray(line.pixel(x, top + y)) < faint;
            if (on && from < 0) from = x;
            if (on || from < 0) continue;
            // Longer than any stroke of a letter; a line lying a little askew shows in pieces of a row.
            if (x - from >= inkPx * 3 / 2) {
                for (int k = from; k < x; ++k) across[y][k] = true;
                pieces.append({ y, from, x - 1, int(pieces.size()) });
            }
            from = -1;
        }
    // Pieces that go on from row to row are one line lying askew, and all along it is that line's:
    // also the short ends where it steps from one row to the next, which would stay as specks.
    for (bool joined = true; joined;) {
        joined = false;
        for (Piece &a : pieces)
            for (const Piece &b : std::as_const(pieces))
                if (b.line < a.line && std::abs(a.y - b.y) <= 1 && a.from <= b.to + inkPx / 2 && b.from <= a.to + inkPx / 2) {
                    a.line = b.line;
                    joined = true;
                }
    }
    for (int id = 0; id < pieces.size(); ++id) {
        double n = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        int left = w, right = -1, rows = 0;
        for (const Piece &p : std::as_const(pieces)) {
            if (p.line != id) continue;
            const double len = p.to - p.from + 1, mid = (p.from + p.to) / 2.0;
            n += len; sx += len * mid; sy += len * p.y; sxy += len * mid * p.y;
            sxx += len * (mid * mid + (len * len - 1.0) / 12.0);
            left = qMin(left, p.from); right = qMax(right, p.to);
            ++rows;
        }
        const double spread = n * sxx - sx * sx;
        if (rows < 2 || spread <= 0.0) continue;
        const double slope = qBound(-0.05, (n * sxy - sx * sy) / spread, 0.05), at = (sy - slope * sx) / n;
        const double half = qMax(1.0, n / (right - left + 1) / 2.0 + 0.75);
        for (int x = qMax(0, left - inkPx / 2); x <= qMin(w - 1, right + inkPx / 2); ++x)
            for (int y = qMax(0, qFloor(at + slope * x - half)); y <= qMin(h - 1, qCeil(at + slope * x + half)); ++y)
                across[y][x] = across[y][x] || qGray(line.pixel(x, top + y)) < faint;
    }
    for (int x = 0; x < w; ++x) {
        // A box's outer edge has its rim a pixel or two beyond where the lines across end.
        const auto crossed = [&](int y) {
            for (int k = qMax(0, x - 2); k <= qMin(w - 1, x + 2); ++k)
                if (across.at(y).at(k)) return true;
            return false;
        };
        int first = 0, last = h - 1;
        for (int y = middle; y >= 0; --y)
            if (crossed(y)) { first = y + 1; break; }
        for (int y = middle; y < h; ++y)
            if (crossed(y)) { last = y - 1; break; }
        // A box's upright line lying a little askew runs through two columns side by side.
        int faintly = 0;
        for (int y = first; y <= last; ++y)
            faintly += qGray(line.pixel(x, top + y)) < faint || (x > 0 && qGray(line.pixel(x - 1, top + y)) < faint)
                    || (x + 1 < w && qGray(line.pixel(x + 1, top + y)) < faint);
        const bool upright = last - first >= 5 && faintly >= (last - first + 1) * 0.85;
        if (upright) rules->append(QRect(x, top + first, 1, last - first + 1));
        for (int y = first; y <= last && !upright; ++y) ink[y][x] = qGray(line.pixel(x, top + y)) < cut;
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0, from = -1; x <= w; ++x) {
            const bool on = x < w && across.at(y).at(x);
            if (on && from < 0) from = x;
            if (!on && from >= 0) { rules->append(QRect(from, top + y, x - from, 1)); from = -1; }
        }
    return ink;
}

// The runs of columns that hold ink, from left to right.
QList<std::pair<int, int>> inkedRuns(const QList<QList<bool>> &ink, int width)
{
    QList<std::pair<int, int>> runs;
    for (int x = 0; x < width; ++x) {
        bool inked = false;
        for (int y = 0; y < ink.size() && !inked; ++y) inked = ink.at(y).at(x);
        if (!inked) continue;
        if (!runs.isEmpty() && runs.last().second == x - 1) runs.last().second = x;
        else runs.append({ x, x });
    }
    return runs;
}

// What is left of a box among the letters' runs: an upright line taller than the letters, as a box's
// outer edge, and a speck above or below them where a line met a rule. They are no letters.
void dropBoxRests(QList<QList<bool>> &ink, QList<std::pair<int, int>> &runs, int top, QList<QRect> *rules)
{
    if (runs.size() < 4) return;
    QList<std::pair<int, int>> rows;
    QList<int> tops, bottoms;
    for (const auto &[from, to] : std::as_const(runs)) {
        int first = int(ink.size()), last = -1;
        for (int y = 0; y < ink.size(); ++y)
            for (int x = from; x <= to; ++x)
                if (ink.at(y).at(x)) { first = qMin(first, y); last = y; break; }
        rows.append({ first, last });
        tops.append(first);
        bottoms.append(last);
    }
    std::sort(tops.begin(), tops.end());
    std::sort(bottoms.begin(), bottoms.end());
    const int usualTop = tops.at(tops.size() / 2), usualBottom = bottoms.at(bottoms.size() / 2);
    const int usual = usualBottom - usualTop + 1, beyond = qMax(2, usual * 3 / 20);
    for (qsizetype i = runs.size() - 1; i >= 0; --i) {
        const auto [from, to] = runs.at(i);
        const auto [first, last] = rows.at(i);
        const bool upright = to - from + 1 <= qMax(3, usual / 6) && first <= usualTop - beyond && last >= usualBottom + beyond;
        const bool speck = last - first + 1 < usual * 3 / 10 && (last < usualTop + beyond || first > usualBottom);
        if (!upright && !speck) continue;
        for (int y = first; y <= last; ++y)
            for (int x = from; x <= to; ++x) ink[y][x] = false;
        rules->append(QRect(from, top + first, to - from + 1, last - first + 1));
        runs.removeAt(i);
    }
}

// A line of letters printed apart with its letters set close, as a word is printed, and how many
// they are; nothing where the line is no such row of letters all alike apart. Where only the
// letters around a place across it stand so, as the figures of a field behind its label, those
// alone, from where to where they reach, across and in the picture's rows, and the form's lines around them.
// Which runs of ink stand as high as a letter of their row may: as the usual one, or higher with its foot on
// the row's line as a capital, a letter with an ascender or one with dots above it, or with its head at the
// row's as a letter with a descender; a smudge over the letters reaches above and below them.
QList<bool> lettersHigh(const QList<int> &highs, const QList<int> &tops, const QList<int> &bottoms, int usual)
{
    QList<int> plainTops, plainBottoms;
    for (int i = 0; i < highs.size(); ++i)
        if (highs.at(i) <= usual * 1.3 && highs.at(i) >= usual * 0.7) {
            plainTops.append(tops.at(i));
            plainBottoms.append(bottoms.at(i));
        }
    QList<bool> out(highs.size(), true);
    if (plainTops.isEmpty()) return out;
    std::sort(plainTops.begin(), plainTops.end());
    std::sort(plainBottoms.begin(), plainBottoms.end());
    const int head = plainTops.at(plainTops.size() / 2), foot = plainBottoms.at(plainBottoms.size() / 2);
    const int near = qMax(2, qRound(usual * 0.08));
    QList<int> ascending, descending;
    for (int i = 0; i < highs.size(); ++i) {
        if (highs.at(i) <= usual * 1.3) continue;
        const bool onFoot = std::abs(bottoms.at(i) - foot) <= near, atHead = std::abs(tops.at(i) - head) <= near;
        out[i] = highs.at(i) <= usual * 2.0 && (onFoot || atHead);
        if (out.at(i) && onFoot) ascending.append(tops.at(i));
        if (out.at(i) && atHead) descending.append(bottoms.at(i));
    }
    // One that reaches as far up as an ascender and as far down as a descender of the row, as a j.
    for (int i = 0; i < highs.size(); ++i) {
        if (out.at(i) || highs.at(i) > usual * 2.4) continue;
        const auto beside = [near](const QList<int> &levels, int at) {
            return std::any_of(levels.cbegin(), levels.cend(), [&](int level) { return std::abs(level - at) <= near; });
        };
        out[i] = beside(ascending, tops.at(i)) && beside(descending, bottoms.at(i));
    }
    return out;
}

QImage closedUp(const QImage &line, int inkPx, double place, int *letters, std::pair<int, int> *span, std::pair<int, int> *rows,
                QList<QRect> *rules)
{
    *rows = { 0, -1 };
    rules->clear();
    if (line.isNull()) return {};
    const int top = qMax(0, line.height() / 2 - qRound(inkPx * 0.62)), bottom = qMin(line.height() - 1, line.height() / 2 + qRound(inkPx * 0.62));
    QList<int> tones;
    for (int y = top; y <= bottom; ++y)
        for (int x = 0; x < line.width(); ++x) tones.append(qGray(line.pixel(x, y)));
    std::sort(tones.begin(), tones.end());
    if (tones.isEmpty()) return {};
    const int paper = tones.at(tones.size() * 9 / 10), dark = tones.at(tones.size() / 50);
    if (paper - dark < 60) return {};
    // A form's thin rule prints lighter than a letter's strokes, in a scan of few tones hardly darker than the paper.
    QList<QList<bool>> ink = lettersInk(line, top, bottom, inkPx, (paper + dark) / 2,
                                        qMax(paper - (paper - dark) * 3 / 10, paper - 45), rules);
    const int high = int(ink.size());
    QList<std::pair<int, int>> runs = inkedRuns(ink, line.width());
    dropBoxRests(ink, runs, top, rules);
    // How high the letters stand in the picture: in a box they are far smaller than the line was taken to be.
    int highest = high, lowest = -1;
    for (int y = 0; y < high; ++y)
        if (ink.at(y).contains(true)) { highest = qMin(highest, y); lowest = y; }
    if (lowest >= highest) *rows = { top + highest, top + lowest };
    const int lettersHighOf = qMax(6, lowest - highest + 1);
    if (runs.size() < 3) return {};
    // The whole line where all its letters stand alike apart. Else from the letter at the place
    // on, as far as the letters stand a like step apart with white as wide as a letter's third
    // between them, which no word printed as usual shows: four of them at least.
    // A place left empty in a row, two of its steps wide, parts it nowhere.
    constexpr double kHole = 2.3;
    QList<int> gaps;
    QList<double> steps;
    for (int i = 1; i < runs.size(); ++i) {
        gaps.append(runs.at(i).first - runs.at(i - 1).second - 1);
        steps.append((runs.at(i).first + runs.at(i).second - runs.at(i - 1).first - runs.at(i - 1).second) / 2.0);
    }
    std::sort(gaps.begin(), gaps.end());
    std::sort(steps.begin(), steps.end());
    // How high each run of ink stands and how dark it is: a smudge or a patch among the letters stands
    // higher or lower than they do, or is gray where they are black.
    QList<int> highs, darks, tops, bottoms;
    for (const auto &[from, to] : std::as_const(runs)) {
        int above = high, below = -1, darkest = 255;
        for (int y = 0; y < high; ++y)
            for (int x = from; x <= to; ++x) {
                if (!ink.at(y).at(x)) continue;
                above = qMin(above, y);
                below = y;
                darkest = qMin(darkest, qGray(line.pixel(x, top + y)));
            }
        highs.append(below - above + 1);
        darks.append(paper - darkest);
        tops.append(above);
        bottoms.append(below);
    }
    // Lower is a point or a dash, which belong to a row; higher than the letters or gray is none of them.
    QList<int> sorted = highs;
    std::sort(sorted.begin(), sorted.end());
    const int usual = sorted.at(sorted.size() / 2);
    const QList<bool> letterHigh = lettersHigh(highs, tops, bottoms, usual);
    const auto [palest0, darkest0] = std::minmax_element(darks.cbegin(), darks.cend());
    int first = 0, last = int(runs.size()) - 1;
    if (gaps.at(gaps.size() / 2) < inkPx / 4 || steps.last() > steps.at(steps.size() / 2) * kHole || letterHigh.contains(false)
            || *palest0 < *darkest0 * 0.45) {
        // The letter at the place: of the runs as dark as the darkest, so no smudge beside it is taken for it.
        const auto letter = [&](int i) { return darks.at(i) >= *darkest0 * 0.45; };
        const auto off = [&](int i) { return std::abs((runs.at(i).first + runs.at(i).second) / 2.0 - place); };
        first = int(std::distance(darks.cbegin(), darkest0));
        for (int i = 0; i < runs.size(); ++i)
            if (letter(i) && off(i) < off(first)) first = i;
        last = first;
        double step = -1.0;
        const int own = highs.at(first), dark0 = darks.at(first);
        const auto like = [&](int i) { return (highs.at(i) <= own * 1.3 || letterHigh.at(i)) && darks.at(i) >= dark0 * 0.45; };
        const auto apart = [&](int i) {
            const double d = (runs.at(i + 1).first + runs.at(i + 1).second - runs.at(i).first - runs.at(i).second) / 2.0;
            const bool hole = step > 0.0 && d >= step * 1.7 && d <= step * kHole;
            if (runs.at(i + 1).first - runs.at(i).second - 1 < lettersHighOf / 3 || (step > 0.0 && !hole && (d < step * 0.7 || d > step * 1.4))) return false;
            if (step < 0.0) step = d;
            return true;
        };
        for (bool grew = true; grew;) {
            grew = false;
            if (last + 1 < runs.size() && like(last + 1) && apart(last)) { ++last; grew = true; }
            if (first > 0 && like(first - 1) && apart(first - 1)) { --first; grew = true; }
        }
        // A letter at an end with another close beside it ends or starts a word, as in a face of
        // fixed pitch a word's last letter before a figure standing alone: it is none set apart.
        const auto joined = [&](int i) { return i >= 0 && i + 1 < runs.size() && runs.at(i + 1).first - runs.at(i).second - 1 < lettersHighOf / 3; };
        const auto ownLetters = [&](int &from, int &to) {
            while (to > from && joined(to)) --to;
            while (from < to && joined(from - 1)) ++from;
        };
        ownLetters(first, last);
        if (last - first + 1 < 4) {
            // No such letters at the place: a row of them elsewhere in the line, as a field of
            // boxes beside the label that was clicked, is told by its reach and a count below zero.
            for (int i = 0, longest = 4; i + longest < runs.size(); ++i) {
                int from = i, to = i;
                step = -1.0;
                while (to + 1 < runs.size() && apart(to)) ++to;
                ownLetters(from, to);
                if (to - from + 1 <= longest) continue;
                longest = to - from + 1;
                *span = { runs.at(from).first, runs.at(to).second };
                *letters = -longest;
            }
            return {};
        }
    }
    if (last - first + 1 > 40) return {};
    runs = runs.mid(first, last - first + 1);
    *span = { runs.first().first, runs.last().second };
    const int between = qMax(3, inkPx / 8), margin = inkPx / 2;
    int wide = 2 * margin + between * int(runs.size() - 1);
    for (const auto &[from, to] : std::as_const(runs)) wide += to - from + 1;
    QImage close(wide, high + 2 * margin, QImage::Format_Grayscale8);
    close.fill(paper);
    int at = margin;
    for (const auto &[from, to] : std::as_const(runs)) {
        for (int y = 0; y < high; ++y)
            for (int x = from; x <= to; ++x) {
                if (!ink.at(y).at(x)) continue;
                const int tone = qGray(line.pixel(x, top + y));
                close.setPixel(at + x - from, margin + y, qRgb(tone, tone, tone));
            }
        at += to - from + 1 + between;
    }
    *letters = int(runs.size());
    return close;
}

// What one look at a line's picture found of letters printed apart.
struct Row {
    QImage image, close;
    QRectF covers;
    double perPt { 0.0 };
    int letters { 0 }, dpi { 0 }, inkPx { 0 };
    std::pair<int, int> span, rows;
    QList<QRect> rules;

    double left() const { return covers.left() + span.first / perPt; }
    double right() const { return covers.left() + (span.second + 1) / perPt; }
};

}

// Letters apart found beside a line are an entry of their own where they stand clear of the line or take
// it in as a piece of theirs: some of a word's own letters, as its last few, are no such entry.
static bool someOf(const QRectF &line, double left, double right)
{
    const double shared = qMin(right, line.right()) - qMax(left, line.left());
    return shared > 0.0 && shared < line.width() * 0.7;
}

bool EditOpener::rereadSpaced(EditOpen &o, TextLayout::OriginalLine &line, bool clicked)
{
    // Letters printed apart, as the figures of a form, are read badly: as
    // several words, with letters too many or too few. Set close, they are
    // read as the word they are, and that reading is the line's where it has
    // as many letters as the line shows.
    constexpr int kInkPx = 40;
    if (!m_ocr || !m_ocr->isReady()) return false;
    const auto look = [&o](const QRectF &rect, double place, int inkPx) {
        Row row;
        row.inkPx = inkPx;
        row.image = o.lineImage(rect, inkPx, &row.dpi, &row.covers);
        if (row.image.isNull() || row.covers.isEmpty()) return Row {};
        row.perPt = row.image.width() / row.covers.width();
        row.close = closedUp(row.image, inkPx, (place - row.covers.left()) * row.perPt, &row.letters, &row.span, &row.rows, &row.rules);
        return row;
    };
    // A click beside the line, as into a field of boxes behind a label that alone was read, looks there;
    // so does one above or below it, where OCR read a dashed rule as a line and the letters not at all.
    // Only the line that was clicked: another line of its paragraph is looked at where it stands. And a
    // click on the paper above or below a line of text opens that line: the letters stand elsewhere only
    // where a rule was read as the line, or where the click fell on letters OCR did not read.
    // A click behind a row of letters apart opens that row.
    const double high = line.rect.height();
    const bool off = clicked && (o.clickPt.y() < line.rect.top() - high * 0.5 || o.clickPt.y() > line.rect.bottom() + high * 0.5)
                  && (o.ruleAlone(line.rect) || o.lettersAt(o.clickPt, high));
    bool beside = off || (clicked && (o.clickPt.x() < line.rect.left() - high || o.clickPt.x() > line.rect.right() + high));
    if (beside && !off && !look(line.rect, line.rect.center().x(), kInkPx).close.isNull()) beside = false;
    const double room = qMax(high, 10.0), place = clicked ? o.clickPt.x() : line.rect.center().x();
    const QRectF first = off ? QRectF(o.clickPt.x() - 4.0 * room, o.clickPt.y() - room * 0.7, 8.0 * room, room * 1.4)
                             : beside ? QRectF(o.clickPt.x() - 4.0 * high, line.rect.top(), 8.0 * high, high) : line.rect;
    Row row = look(first, place, kInkPx);
    if (row.perPt <= 0.0) return false;
    // Letters far smaller than the line, as figures in a box, are looked at again as large as a line's.
    if (const int inked = row.rows.second - row.rows.first + 1; inked >= 6 && inked < kInkPx * 0.7) {
        row = look(first, place, qMin(160, qRound(double(kInkPx) * kInkPx * 0.8 / inked)));
        if (row.perPt <= 0.0) return false;
    }
    // OCR may read some letters of such a row as a block of their own, or the row in pieces: it goes
    // on as far as letters stand a like step apart beside it.
    const qsizetype lettersRead = line.text.size() - line.text.count(u' ');
    for (int round = 0; round < 4; ++round) {
        const bool piece = round == 0 && row.close.isNull() && row.letters == 0 && lettersRead <= 3;
        if (row.close.isNull() && !piece) break;
        const double step = piece ? line.rect.height() * 1.6 : (row.span.second - row.span.first) / row.perPt / qMax(1, row.letters - 1);
        const QRectF from = piece ? line.rect : beside ? QRectF(QPointF(row.left(), first.top()), QPointF(row.right(), first.bottom()))
                          : QRectF(QPointF(qMin(row.left(), line.rect.left()), line.rect.top()), QPointF(qMax(row.right(), line.rect.right()), line.rect.bottom()));
        const Row wide = look(from.adjusted(-2.5 * step, 0.0, 2.5 * step, 0.0), piece ? line.rect.center().x() : (row.left() + row.right()) / 2.0,
                              row.inkPx);
        if (wide.close.isNull() || wide.letters <= (piece ? 3 : row.letters) || (!piece && wide.letters > row.letters + 6)) break;
        row = wide;
    }
    // Letters standing off the line as it was read, as beside a rule OCR took for it, are looked at again framed on their own.
    if (!row.close.isNull()) {
        const double top = row.covers.top() + row.rows.first / row.perPt, bottom = row.covers.top() + (row.rows.second + 1) / row.perPt;
        if (top < line.rect.top() - (bottom - top) * 0.25 || bottom > line.rect.bottom() + (bottom - top) * 0.25) {
            const Row own = look(QRectF(QPointF(row.left(), top), QPointF(row.right(), bottom)), (row.left() + row.right()) / 2.0, kInkPx);
            if (!own.close.isNull() && own.letters == row.letters) row = own;
        }
    }
    if (beside && !off && !row.close.isNull() && lettersRead > 3 && someOf(line.rect, row.left(), row.right())) return false;
    if (row.close.isNull()) {
        if (!clicked || beside || row.letters >= 0 || !endBeforeRow(o, line, row.left(), row.right())) return false;
        o.wipe(row.rules, row.covers, row.perPt);
        return true;
    }
    QString read = m_ocr->recognizeLine(row.close, row.dpi).simplified();
    read.remove(u' ');
    // The letters apart alone are the line where it holds more, as a label before them.
    const double left = row.left(), right = row.right();
    const bool part = beside || left > line.rect.left() + line.rect.height() || right < line.rect.right() - line.rect.height();
    if (read.size() != row.letters) return false;
    // A form's lines around the letters are none of the line's ink: what measures it from here on sees the letters alone.
    o.wipe(row.rules, row.covers, row.perPt);
    const double inkTop = row.covers.top() + row.rows.first / row.perPt, inkBottom = row.covers.top() + (row.rows.second + 1) / row.perPt;
    // Or the line was read where a rule stood, above or below the letters.
    const double reach = (inkBottom - inkTop) * 0.25;
    const bool boxed = line.rect.height() > (inkBottom - inkTop) * 1.25
                    || inkTop < line.rect.top() - reach || inkBottom > line.rect.bottom() + reach;
    const bool beyond = left < line.rect.left() - line.rect.height() * 0.5 || right > line.rect.right() + line.rect.height() * 0.5;
    if (read == line.text && !part && !boxed && !beyond) return false;
    if (read != line.text || part || beyond) {
        line.text = read;
        line.charX.clear();
        line.objectStart.clear();
    }
    if (boxed || off) {
        line.rect = QRectF(QPointF(left, inkTop), QPointF(right, inkBottom));
        if (line.baseline < inkTop + line.rect.height() * 0.5 || line.baseline > inkBottom + line.rect.height() * 0.15) line.baseline = inkBottom;
    } else if (part || beyond) {
        // Measured again by itself: beside its label a field's box counted as the line's ink.
        line.rect = QRectF(QPointF(left, line.rect.top()), QPointF(right, line.rect.bottom()));
        QList<TextLayout::OriginalLine> alone { line };
        o.narrowToInk(alone);
        line.rect = QRectF(QPointF(left, alone.first().rect.top()), QPointF(right, alone.first().rect.bottom()));
    }
    return true;
}

bool EditOpener::closeApart(EditOpen &o, TextLayout::OriginalLine &line) const
{
    // Three and more letters or figures that stand apart, each a run of ink with white as wide as a quarter of
    // the line's height before the next, or each read as a word a like step from the next, are one word set
    // apart, however OCR parted it: its letters go each on their own ink, and what is typed takes the row's step.
    QString letters = line.text;
    letters.remove(u' ');
    if (letters.size() < 3 || letters.size() == line.text.size()
            || !std::all_of(letters.cbegin(), letters.cend(), [](QChar c) { return c.isLetterOrNumber(); }))
        return false;
    const double high = line.rect.height(), right = line.rect.right() + high * 0.3;
    QList<std::pair<double, double>> runs;
    double from = line.rect.left() - high * 0.3;
    for (const auto &[start, end] : o.gaps(QRectF(QPointF(from, line.rect.top()), QPointF(right, line.rect.bottom())))) {
        if (start > from + 1e-6) runs.append({ from, start });
        from = end;
    }
    if (from < right - 1e-6) runs.append({ from, right });
    bool apart = runs.size() == letters.size();
    for (qsizetype n = 1; n < runs.size() && apart; ++n) apart = runs.at(n).first - runs.at(n - 1).second >= high * 0.25;
    if (!apart && line.charX.size() == line.text.size()) {
        QList<double> starts;
        bool single = true;
        for (qsizetype c = 0; c < line.text.size() && single; ++c) {
            if (line.text.at(c) == u' ') continue;
            single = (c == 0 || line.text.at(c - 1) == u' ') && (c + 1 == line.text.size() || line.text.at(c + 1) == u' ');
            starts.append(line.charX.at(c));
        }
        double least = 1e9, most = 0.0;
        for (qsizetype i = 1; i < starts.size(); ++i) {
            least = qMin(least, starts.at(i) - starts.at(i - 1));
            most = qMax(most, starts.at(i) - starts.at(i - 1));
        }
        apart = single && least > high * 0.5 && most <= least * 1.4;
    }
    if (!apart) return false;
    line.text = letters;
    line.charX.clear();
    line.objectStart.clear();
    return true;
}

bool EditOpener::keepFigures(EditOpen &o, TextLayout::OriginalLine &line)
{
    // A row of figures with a letter among them, as OCR reads from a smudge or a patch between them:
    // the figures on the clicked side of it are the line, the rest stays the scan's pixels.
    if (line.charX.size() != line.text.size()) return false;
    QList<int> at;
    for (int i = 0; i < line.text.size(); ++i)
        if (!line.text.at(i).isSpace()) at.append(i);
    const qsizetype digits = std::count_if(line.text.cbegin(), line.text.cend(), [](QChar c) { return c.isDigit(); });
    const qsizetype alphas = std::count_if(line.text.cbegin(), line.text.cend(), [](QChar c) { return c.isLetter(); });
    if (digits < 3 || alphas == 0 || alphas * 2 > digits || at.size() >= 16) return false;
    // Only figures each apart: a date or a number with a letter misread in it is a word to read again whole.
    const QStringList each = line.text.split(u' ', Qt::SkipEmptyParts);
    if (std::any_of(each.cbegin(), each.cend(), [](const QString &one) { return one.size() > 1; })) return false;
    const auto foreign = [&](int k) { return line.text.at(at.at(k)).isLetter(); };
    // The run of figures the click fell on or nearest to.
    int first = -1, last = -1;
    double nearest = 1e9;
    bool any = false;
    for (int k = 0; k < at.size(); ++k) {
        if (foreign(k)) { any = true; continue; }
        int e = k;
        while (e + 1 < at.size() && !foreign(e + 1)) ++e;
        const double left = line.charX.at(at.at(k)), right = e + 1 < at.size() ? line.charX.at(at.at(e + 1)) : line.endX;
        const double off = qMax(0.0, qMax(left - o.clickPt.x(), o.clickPt.x() - right));
        if (off < nearest) { nearest = off; first = k; last = e; }
        k = e;
    }
    if (!any || first < 0 || last - first + 1 < 2) return false;
    const int a = at.at(first), b = at.at(last) + 1;
    const double end = last + 1 < at.size() ? line.charX.at(at.at(last + 1)) : line.endX;
    line.rect = QRectF(QPointF(line.charX.at(a), line.rect.top()), QPointF(end, line.rect.bottom()));
    line.endX = end;
    line.text = line.text.mid(a, b - a);
    line.charX = line.charX.mid(a, b - a);
    line.objectStart = line.objectStart.mid(a, b - a);
    // Its ink tells how high it stands; what is beside it, the smudge too, is none of it.
    QList<TextLayout::OriginalLine> alone { line };
    o.narrowToInk(alone);
    line.rect = QRectF(QPointF(line.rect.left(), alone.first().rect.top()), QPointF(line.rect.right(), alone.first().rect.bottom()));
    // Figures each apart are the word they are, set on their ink like any row of them read close.
    const QStringList apart = line.text.split(u' ', Qt::SkipEmptyParts);
    if (apart.size() >= 2 && std::all_of(apart.cbegin(), apart.cend(), [](const QString &one) { return one.size() == 1; })) {
        line.text = apart.join(QString());
        line.charX.clear();
        line.objectStart.clear();
    }
    return true;
}

bool EditOpener::endBeforeRow(EditOpen &o, TextLayout::OriginalLine &line, double from, double to)
{
    // A row of figures apart beside what was clicked is an entry of its own: the line ends before it.
    const bool after = o.clickPt.x() < from;
    if (line.charX.size() != line.text.size() || (!after && o.clickPt.x() <= to)) return false;
    int a = 0, b = int(line.text.size());
    while (after && b > 0 && line.charX.at(b - 1) >= from - line.rect.height() * 0.3) --b;
    while (!after && a < b && line.charX.at(a) <= to) ++a;
    while (b > a && line.text.at(b - 1).isSpace()) --b;
    // What OCR read from the box's first line, a bracket or a stroke left open, is no word of the label.
    if (int w = int(line.text.lastIndexOf(u' ', b - 1)) + 1; after && w > a && w < b && QStringLiteral("[({|").contains(line.text.at(w))) {
        const QChar open = line.text.at(w), close = line.text.at(b - 1);
        const bool shut = b - w > 1 && ((open == u'[' && close == u']') || (open == u'(' && close == u')') || (open == u'{' && close == u'}'));
        if (!shut) b = w;
    } else if (after && w > a + 1 && b - w == 1 && QStringLiteral("Il1!/\\").contains(line.text.at(w))
               && line.charX.at(w) - line.charX.at(w - 1) > line.rect.height() * 1.5) {
        // So is a lone stroke far behind the label, as the box's edge read as a letter.
        b = w;
    }
    while (b > a && line.text.at(b - 1).isSpace()) --b;
    while (a < b && line.text.at(a).isSpace()) ++a;
    if (b - a < 1 || b - a == line.text.size()) return false;
    const double end = b < line.text.size() ? line.charX.at(b) : from;
    line.endX = b < line.text.size() ? line.charX.at(b) : line.endX;
    line.text = line.text.mid(a, b - a);
    line.charX = line.charX.mid(a, b - a);
    line.objectStart = line.objectStart.mid(a, b - a);
    line.rect = QRectF(QPointF(after ? line.rect.left() : to, line.rect.top()), QPointF(after ? qMin(from, end) : line.rect.right(), line.rect.bottom()));
    QList<TextLayout::OriginalLine> alone { line };
    o.narrowToInk(alone);
    line.rect = QRectF(QPointF(line.rect.left(), alone.first().rect.top()), QPointF(line.rect.right(), alone.first().rect.bottom()));
    return true;
}

#endif
