#include "ui/edit/ScanCleanup.hpp"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace ScanCleanup {

namespace {

// The gaps between the words of a line, from the end of one to the start of the next.
QList<std::pair<double, double>> wordGaps(const TextLayout::OriginalLine &line)
{
    QList<std::pair<double, double>> gaps;
    for (qsizetype c = 1; c < line.text.size() && line.charX.size() == line.text.size(); ++c) {
        if (line.text.at(c) != u' ' || line.text.at(c - 1) == u' ') continue;
        qsizetype n = c;
        while (n < line.text.size() && line.text.at(n) == u' ') ++n;
        if (n < line.text.size()) gaps.append({ line.charX.at(c), line.charX.at(n) });
    }
    return gaps;
}

// Whether the lines are set in a fixed pitch face: every longer word is as wide per letter as the others.
bool fixedPitch(const QList<const QList<TextLayout::OriginalLine> *> &blocks)
{
    QList<double> pitches;
    for (const QList<TextLayout::OriginalLine> *lines : blocks)
        for (const TextLayout::OriginalLine &line : *lines) {
            if (line.charX.size() != line.text.size()) continue;
            for (qsizetype c = 0; c < line.text.size();) {
                if (line.text.at(c) == u' ') { ++c; continue; }
                qsizetype e = c;
                while (e < line.text.size() && line.text.at(e) != u' ') ++e;
                const double end = e < line.text.size() ? line.charX.at(e) : line.endX;
                if (e - c >= 4) pitches.append((end - line.charX.at(c)) / double(e - c));
                c = e;
            }
        }
    if (pitches.size() < 6) return false;
    std::sort(pitches.begin(), pitches.end());
    const double low = pitches.at(pitches.size() / 10), high = pitches.at(pitches.size() - 1 - pitches.size() / 10);
    return high <= low * 1.1;
}

// A mark told by its ink, which OCR reads as a letter or another sign: a round
// filled dot halfway up the small letters and at most half the size tall is a
// bullet or a middle dot, a flat bar there a hyphen or a dash.
QString markOf(const InkMetrics::Blob &ink, double baseline, double em)
{
    if (ink.rect.isEmpty() || em <= 0.0) return {};
    const double w = ink.rect.width() / em, h = ink.rect.height() / em;
    const double rise = (baseline - ink.rect.center().y()) / em;
    if (rise < 0.15 || rise > 0.6) return {};
    if (ink.fill >= 0.55 && w >= h * 0.6 && w <= h * 1.6 && w <= 0.55 && h <= 0.5)
        return w >= 0.18 ? QStringLiteral("\u2022") : QStringLiteral("\u00b7");
    if (ink.fill >= 0.6 && h <= 0.12 && w >= h * 2.5)
        return w < 0.4 ? QStringLiteral("-") : w < 0.75 ? QStringLiteral("\u2013") : QStringLiteral("\u2014");
    return {};
}

}

namespace {

struct Channel { double from, to, nextLeast, nextMost, endLeast, endMost; int gaps; };

// The white channels running down through all of the lines. A line ending
// before a channel or starting after it leaves it free; only the lines with
// a gap there count and tell where words start.
bool channelsOf(const QList<TextLayout::OriginalLine> &lines, QList<Channel> *found, double *em)
{
    constexpr double kOpen = 1e9;
    QList<Channel> channels;
    for (int i = 0; i < lines.size(); ++i) {
        const TextLayout::OriginalLine &line = lines.at(i);
        if (line.charX.size() != line.text.size() || line.charX.isEmpty()) return false;
        // Capitals and ascenders reach about three quarters of the size.
        *em = qMax(*em, (line.baseline - line.rect.top()) / 0.75);
        QList<std::pair<double, double>> gaps = wordGaps(line);
        gaps.prepend({ -kOpen, line.charX.first() });
        gaps.append({ line.endX, kOpen });
        const auto real = [](double g0, double g1) { return g0 > -kOpen && g1 < kOpen; };
        if (i == 0) {
            for (const auto &[g0, g1] : std::as_const(gaps))
                channels.append(real(g0, g1) ? Channel { g0, g1, g1, g1, g0, g0, 1 }
                                             : Channel { g0, g1, kOpen, -kOpen, kOpen, -kOpen, 0 });
            continue;
        }
        QList<Channel> common;
        for (const Channel &a : std::as_const(channels))
            for (const auto &[g0, g1] : std::as_const(gaps)) {
                if (qMin(a.to, g1) <= qMax(a.from, g0)) continue;
                Channel c { qMax(a.from, g0), qMin(a.to, g1), a.nextLeast, a.nextMost, a.endLeast, a.endMost, a.gaps };
                if (real(g0, g1)) {
                    c.nextLeast = qMin(c.nextLeast, g1);
                    c.nextMost = qMax(c.nextMost, g1);
                    c.endLeast = qMin(c.endLeast, g0);
                    c.endMost = qMax(c.endMost, g0);
                    ++c.gaps;
                }
                common.append(c);
            }
        channels = common;
    }
    channels.removeIf([](const Channel &c) { return c.gaps == 0 || c.from <= -kOpen || c.to >= kOpen; });
    *found = channels;
    return true;
}

// Letters set apart, as the figures of a form's row, are one entry, also in groups or with a place left
// empty: the white between them runs down through the rows around and is no channel between columns.
bool lettersApart(const TextLayout::OriginalLine &line)
{
    const qsizetype n = line.text.size();
    if (line.charX.size() != n) return false;
    QList<double> starts;
    for (qsizetype c = 0; c < n; ++c) {
        if (line.text.at(c) == u' ') continue;
        if ((c > 0 && line.text.at(c - 1) != u' ') || (c + 1 < n && line.text.at(c + 1) != u' ')) return false;
        starts.append(line.charX.at(c));
    }
    if (starts.size() < 3) return false;
    double least = 1e9, most = 0.0;
    for (qsizetype i = 1; i < starts.size(); ++i) {
        least = qMin(least, starts.at(i) - starts.at(i - 1));
        most = qMax(most, starts.at(i) - starts.at(i - 1));
    }
    return least > 0.0 && most <= least * 2.2;
}

}

// Lines of columns OCR read as one are parted at the white channel running
// down between the columns: a gap at the same place in every line, after
// which each starts at the same place, and in two lines only before which
// each ends at the same place too, or where a line around it on the page
// has that gap and starts there too; or, over several lines, one as wide
// as a table's. Only the column clicked is kept.
bool keepColumn(QList<TextLayout::OriginalLine> &lines, const QList<TextLayout::OriginalLine> &around,
                double clickX)
{
    if (lines.isEmpty()) return false;
    if (std::all_of(lines.cbegin(), lines.cend(), lettersApart)) return false;
    QList<Channel> channels;
    double em = 0.0;
    if (!channelsOf(lines, &channels, &em)) return false;
    // A line read over columns whose lines stand a little apart reaches above
    // its own capitals: the lines around tell its size too.
    QList<double> ems;
    const auto sizeOf = [&ems](const TextLayout::OriginalLine &line) {
        if (line.baseline > line.rect.top()) ems.append((line.baseline - line.rect.top()) / 0.75);
    };
    std::for_each(lines.cbegin(), lines.cend(), sizeOf);
    std::for_each(around.cbegin(), around.cend(), sizeOf);
    std::sort(ems.begin(), ems.end());
    if (!ems.isEmpty()) em = qMin(em, ems.at(ems.size() / 2));
    // A justified line's last word may start where the one above starts: a
    // column goes on with more than one word after the channel.
    const auto continues = [&](const Channel &c) {
        for (const TextLayout::OriginalLine &line : std::as_const(lines)) {
            int after = 0;
            for (qsizetype k = 0; k < line.text.size(); ++k)
                if (line.text.at(k) != u' ' && (k == 0 || line.text.at(k - 1) == u' ') && line.charX.at(k) >= c.to - 0.5) ++after;
            if (after == 1) return false;
        }
        return true;
    };
    const double close = qMax(1.0, em * 0.1);
    // A line around, not one of these, that starts alike after the channel,
    // read as a line of its own or with a gap over the channel.
    const auto backed = [&](const Channel &c) {
        for (const TextLayout::OriginalLine &other : around) {
            if (other.charX.isEmpty() || std::any_of(lines.cbegin(), lines.cend(), [&](const TextLayout::OriginalLine &line) {
                    return std::abs(line.baseline - other.baseline) < em * 0.5; }))
                continue;
            if (std::abs(other.charX.first() - c.nextLeast) <= close && std::abs(other.charX.first() - c.nextMost) <= close)
                return true;
            for (const auto &[g0, g1] : wordGaps(other))
                if (g0 <= c.from + close && g1 >= c.to - close
                        && std::abs(g1 - c.nextLeast) <= close && std::abs(g1 - c.nextMost) <= close)
                    return true;
        }
        return false;
    };
    // A narrow justified column spreads its words as wide as a table its
    // cells: a channel whose lines start unlike after it is a table's only
    // where it is clearly wider than the other gaps of those lines.
    const auto standsOut = [&](const Channel &c) {
        QList<double> others;
        for (const TextLayout::OriginalLine &line : std::as_const(lines))
            for (const auto &[g0, g1] : wordGaps(line))
                if (g1 <= c.from || g0 >= c.to) others.append(g1 - g0);
        if (others.isEmpty()) return true;
        std::sort(others.begin(), others.end());
        return c.to - c.from >= others.at(others.size() / 2) * 1.6;
    };
    // In a fixed pitch face words start below each other all over the lines:
    // a channel is a column's only where it is wider than the gaps between words too.
    const bool grid = fixedPitch({ &lines, &around });
    channels.removeIf([&](const Channel &c) {
        const bool alike = c.nextMost - c.nextLeast <= close;
        const bool aligned = alike && (c.gaps >= 3 || ((c.gaps == 2 && c.endMost - c.endLeast <= close) || backed(c))
                                                       && continues(c));
        // A line alone may space its words that wide.
        if (!aligned && (c.gaps < 2 || !standsOut(c))) return true;
        if (aligned && grid && !standsOut(c)) return true;
        return c.to - c.from < em * (aligned ? 0.8 : 2.0);
    });
    if (channels.isEmpty() || em <= 0.0) return false;
    double from = -1e9, to = 1e9;
    for (const Channel &c : std::as_const(channels)) {
        if (c.to <= clickX) from = qMax(from, c.to);
        else if (c.from >= clickX) to = qMin(to, c.from);
        else return false;
    }
    QList<TextLayout::OriginalLine> kept;
    for (const TextLayout::OriginalLine &line : std::as_const(lines)) {
        qsizetype first = -1, end = -1;
        for (qsizetype c = 0; c < line.text.size(); ++c)
            if (line.text.at(c) != u' ' && line.charX.at(c) >= from - em * 0.25 && line.charX.at(c) < to) {
                if (first < 0) first = c;
                end = c + 1;
            }
        if (first < 0) continue;
        TextLayout::OriginalLine part = line;
        part.text = line.text.mid(first, end - first);
        part.charX = line.charX.mid(first, end - first);
        if (line.objectStart.size() == line.text.size()) part.objectStart = line.objectStart.mid(first, end - first);
        part.endX = end < line.text.size() ? line.charX.at(end) : line.endX;
        part.rect.setLeft(qMax(line.rect.left(), part.charX.first() - em * 0.25));
        part.rect.setRight(qMin(line.rect.right(), part.endX + em * 0.25));
        kept.append(part);
    }
    if (kept.isEmpty()) return false;
    lines = kept;
    return true;
}

bool keepSide(QList<TextLayout::OriginalLine> &lines, double clickX)
{
    // Letters read as one word across a gap far wider than the steps between
    // its other letters, or after letters standing further right, are two
    // entries side by side, as figures in two columns of a form: the line is
    // the one that was clicked.
    bool changed = false;
    for (TextLayout::OriginalLine &line : lines) {
        const int n = int(line.text.size());
        if (n < 3 || line.charX.size() != n) continue;
        const auto inWord = [&](int c) { return !line.text.at(c).isSpace() && !line.text.at(c - 1).isSpace(); };
        QList<double> steps;
        for (int c = 1; c < n; ++c)
            if (inWord(c) && line.charX.at(c) > line.charX.at(c - 1)) steps.append(line.charX.at(c) - line.charX.at(c - 1));
        if (steps.size() < 2) continue;
        std::sort(steps.begin(), steps.end());
        const double usual = steps.at(steps.size() / 2), high = line.rect.height();
        QList<int> starts { 0 };
        for (int c = 1; c < n; ++c) {
            const double step = line.charX.at(c) - line.charX.at(c - 1);
            if (inWord(c) && (step > qMax(high * 4.0, usual * 4.0) || step < -high)) starts.append(c);
        }
        if (starts.size() < 2) continue;
        starts.append(n);
        int best = 0;
        double least = 1e18;
        for (int p = 0; p + 1 < starts.size(); ++p) {
            const double left = line.charX.at(starts.at(p)), right = line.charX.at(starts.at(p + 1) - 1) + usual;
            const double far = clickX < left ? left - clickX : clickX > right ? clickX - right : 0.0;
            if (far < least) { least = far; best = p; }
        }
        const int a = starts.at(best), b = starts.at(best + 1);
        const double end = line.charX.at(b - 1) + usual;
        line.endX = b == n && line.endX > line.charX.at(b - 1) && line.endX < end + usual * 2.0 ? line.endX : end;
        line.text = line.text.mid(a, b - a);
        line.charX = line.charX.mid(a, b - a);
        line.objectStart = line.objectStart.mid(a, b - a);
        line.rect = QRectF(QPointF(line.charX.first(), line.rect.top()), QPointF(line.endX, line.rect.bottom()));
        changed = true;
    }
    return changed;
}

// A speck of dust OCR read as a line of its own, far smaller than the lines
// around, is no line; a text layer gives it a line's height.
// Pieces of one line, as of a justified narrow column parted at its wide
// gaps, stand on one baseline one after the other within the text found
// there: they are one line again.
bool joinPieces(QList<TextLayout::OriginalLine> &lines, const QRectF &within)
{
    if (within.isEmpty()) return false;
    std::stable_sort(lines.begin(), lines.end(), [](const TextLayout::OriginalLine &a, const TextLayout::OriginalLine &b) {
        return a.baseline < b.baseline;
    });
    const auto sameBaseline = [](const TextLayout::OriginalLine &a, const TextLayout::OriginalLine &b) {
        return std::abs(a.baseline - b.baseline) <= qMax(a.rect.height(), b.rect.height()) * 0.25;
    };
    // Each run on one baseline from left to right.
    for (qsizetype i = 0; i < lines.size();) {
        qsizetype e = i + 1;
        while (e < lines.size() && sameBaseline(lines.at(i), lines.at(e))) ++e;
        std::sort(lines.begin() + i, lines.begin() + e, [](const TextLayout::OriginalLine &a, const TextLayout::OriginalLine &b) {
            return a.rect.left() < b.rect.left();
        });
        i = e;
    }
    bool joined = false;
    for (qsizetype i = 0; i + 1 < lines.size();) {
        TextLayout::OriginalLine &a = lines[i];
        const TextLayout::OriginalLine &b = lines.at(i + 1);
        const QRectF area = within.adjusted(-2.0, -2.0, 2.0, 2.0);
        if (!sameBaseline(a, b) || a.charX.size() != a.text.size() || b.charX.size() != b.text.size()
                || b.charX.isEmpty() || b.charX.first() <= a.endX
                || !area.contains(a.rect.center()) || !area.contains(b.rect.center())) {
            ++i;
            continue;
        }
        const bool objects = a.objectStart.size() == a.text.size() && b.objectStart.size() == b.text.size();
        a.text += u' ' + b.text;
        a.charX.append(a.endX);
        a.charX += b.charX;
        if (objects) a.objectStart += QList<bool>{ false } + b.objectStart;
        else a.objectStart.clear();
        a.endX = b.endX;
        a.rect = a.rect.united(b.rect);
        lines.removeAt(i + 1);
        joined = true;
    }
    std::stable_sort(lines.begin(), lines.end(), [](const TextLayout::OriginalLine &a, const TextLayout::OriginalLine &b) {
        return a.rect.top() < b.rect.top();
    });
    return joined;
}

bool dropSpecks(QList<TextLayout::OriginalLine> &lines)
{
    if (lines.size() < 2) return false;
    QList<double> heights;
    for (const TextLayout::OriginalLine &line : std::as_const(lines)) heights.append(line.rect.height());
    std::sort(heights.begin(), heights.end());
    const double low = heights.at(heights.size() / 2) * 0.4;
    return lines.removeIf([low](const TextLayout::OriginalLine &line) {
        return (line.rect.height() < low || line.rect.width() < low)
            && std::none_of(line.text.cbegin(), line.text.cend(), [](QChar c) { return c.isLetterOrNumber(); });
    }) > 0;
}

// Marks OCR reads from specks of dust: alone at either end of a line, or a
// quote or comma before its first word that nothing in the line closes.
bool dropSpeckMarks(QList<TextLayout::OriginalLine> &lines)
{
    static const QString marks = QStringLiteral(".,'`\u00b4\u201a|_~^\u00b0");
    const auto speck = [](QStringView word) {
        return std::all_of(word.cbegin(), word.cend(), [](QChar c) { return marks.contains(c); });
    };
    bool dropped = false;
    for (TextLayout::OriginalLine &line : lines) {
        // The words as stretches of the line, so the places of what is kept go along.
        QList<std::pair<qsizetype, qsizetype>> words;
        for (qsizetype c = 0; c < line.text.size();) {
            if (line.text.at(c) == u' ') { ++c; continue; }
            qsizetype e = c;
            while (e < line.text.size() && line.text.at(e) != u' ') ++e;
            words.append({ c, e - c });
            c = e;
        }
        if (words.isEmpty()) continue;
        const auto wordAt = [&](int k) { return QStringView(line.text).mid(words.at(k).first, words.at(k).second); };
        const qsizetype was = words.size();
        while (words.size() > 1 && speck(wordAt(int(words.size()) - 1))) words.removeLast();
        while (words.size() > 1 && speck(wordAt(0))) words.removeFirst();
        QString rest;
        for (int k = 1; k < words.size(); ++k) { rest += wordAt(k); rest += u' '; }
        const QStringView first = wordAt(0);
        const bool lead = first.size() > 1 && QStringLiteral(",'`\u00b4\u201a").contains(first.front()) && first.at(1).isLetterOrNumber()
                && !rest.contains(u'\'') && !rest.contains(QChar(0x2018)) && !rest.contains(QChar(0x2019))
                && !first.mid(1).contains(u'\'') && !first.contains(QChar(0x2018)) && !first.contains(QChar(0x2019));
        if (lead) { ++words[0].first; --words[0].second; }
        if (words.size() == was && !lead) continue;
        const bool placed = line.charX.size() == line.text.size();
        const bool started = line.objectStart.size() == line.text.size();
        QString text;
        QList<double> charX;
        QList<bool> objectStart;
        for (int k = 0; k < words.size(); ++k) {
            const auto [from, length] = words.at(k);
            if (k > 0) {
                const qsizetype after = words.at(k - 1).first + words.at(k - 1).second;
                text += u' ';
                if (placed) charX.append(line.charX.at(after));
                if (started) objectStart.append(false);
            }
            text += QStringView(line.text).mid(from, length);
            if (placed) charX += line.charX.mid(from, length);
            if (started) objectStart += line.objectStart.mid(from, length);
        }
        const qsizetype end = words.last().first + words.last().second;
        if (placed && end < line.text.size()) line.endX = line.charX.at(end);
        line.text = text;
        if (placed) line.charX = charX;
        if (started) line.objectStart = objectStart;
        dropped = true;
    }
    return dropped;
}

// A number and a word OCR read as one, as "1von" or "7%", stand a word's
// gap apart in the scan. Where a line shows as many gaps as wide as a space
// more than its text has spaces as it has such places, each gets its space,
// and the words stand between the gaps the scan shows. Other words stay as
// OCR parted them, which tells letters from gaps better than their widths do.
bool partFigures(QList<TextLayout::OriginalLine> &lines, double spacePt,
                 const std::function<QList<std::pair<double, double>>(int line)> &gapsOf)
{
    constexpr double kGap = 0.8;
    bool parted = false;
    // A code holds capitals beside figures with no gap between them.
    const auto apart = [](QChar a, QChar b) {
        return (a.isDigit() && (b.isLower() || b == u'%')) || (a.isLower() && b.isDigit());
    };
    for (int i = 0; i < lines.size() && spacePt > 0.0; ++i) {
        TextLayout::OriginalLine &line = lines[i];
        if (line.charX.size() != line.text.size() || line.text.contains(QStringLiteral("  "))) continue;
        QList<qsizetype> places;
        for (qsizetype c = 1; c < line.text.size(); ++c)
            if (apart(line.text.at(c - 1), line.text.at(c))) places.append(c);
        if (places.isEmpty()) continue;
        // The gaps between words, and where the line's ink starts and ends.
        QList<std::pair<double, double>> gaps = gapsOf(i);
        double from = line.rect.left(), to = line.rect.right();
        if (!gaps.isEmpty() && gaps.first().first <= line.rect.left() + 0.5) from = gaps.takeFirst().second;
        if (!gaps.isEmpty() && gaps.last().second >= line.rect.right() - 0.5) to = gaps.takeLast().first;
        gaps.removeIf([spacePt](const auto &g) { return g.second - g.first < spacePt * kGap; });
        if (gaps.size() != line.text.count(u' ') + places.size()) continue;
        QString text = line.text;
        for (qsizetype k = places.size() - 1; k >= 0; --k) text.insert(places.at(k), u' ');
        // Each word between two gaps, its letters spread over its ink.
        QList<double> charX;
        qsizetype word = 0;
        for (qsizetype c = 0; c < text.size();) {
            qsizetype e = c;
            while (e < text.size() && text.at(e) != u' ') ++e;
            const double left = word == 0 ? from : gaps.at(word - 1).second, right = word < gaps.size() ? gaps.at(word).first : to;
            for (qsizetype k = c; k < e; ++k) charX.append(left + (right - left) * double(k - c) / double(e - c));
            if (e < text.size()) charX.append(right);
            c = e + 1;
            ++word;
        }
        line.text = text;
        line.charX = charX;
        line.endX = to;
        line.objectStart = QList<bool>(text.size(), false);
        parted = true;
    }
    return parted;
}

// Words of a sign or two whose ink is a mark take that mark.
bool readMarks(QList<TextLayout::OriginalLine> &lines, const LineMeasure &measure,
               const std::function<InkMetrics::Blob(const QRectF &)> &blobAt)
{
    bool changed = false;
    for (int i = 0; i < lines.size(); ++i) {
        TextLayout::OriginalLine &line = lines[i];
        if (line.charX.size() != line.text.size()) continue;
        for (qsizetype c = 0; c < line.text.size();) {
            if (line.text.at(c).isSpace()) { ++c; continue; }
            qsizetype e = c;
            while (e < line.text.size() && !line.text.at(e).isSpace()) ++e;
            qsizetype next = e;
            while (next < line.text.size() && line.text.at(next).isSpace()) ++next;
            const QString token = line.text.mid(c, e - c);
            const double at = line.charX.at(c);
            const auto [em, base] = measure(i, at);
            if (em > 0.0 && token.size() <= 2
                    && std::none_of(token.cbegin(), token.cend(), [](QChar ch) { return ch.isDigit(); })) {
                const double left = at - em * 0.3;
                const double right = next < line.text.size() ? line.charX.at(next) - em * 0.08 : line.endX + em * 0.15;
                const QString mark = right > left
                    ? markOf(blobAt(QRectF(QPointF(left, base - em), QPointF(right, base + em * 0.15))), base, em)
                    : QString();
                if (!mark.isEmpty() && mark != token) {
                    const qsizetype extra = token.size() - mark.size();
                    line.text.replace(c, token.size(), mark);
                    line.charX.remove(c + 1, extra);
                    if (line.objectStart.size() == line.charX.size() + extra) line.objectStart.remove(c + 1, extra);
                    next -= extra;
                    changed = true;
                }
            }
            c = next;
        }
    }
    return changed;
}

}
