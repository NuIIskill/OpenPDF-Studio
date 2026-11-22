#include "ui/edit/EditOpen.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/document/DocumentSource.hpp"
#include "ui/edit/ScanCleanup.hpp"

#include <QFontMetricsF>
#include <QRegularExpression>
#include <QTransform>
#include <QtMath>

namespace {

bool isMark(const QString &word)
{
    return word.size() == 1 && QStringLiteral("\u2022\u00b7-\u2013\u2014").contains(word.front());
}

// The lines of a scan as the matched face sets them: each on its own
// baseline and each word on the middle of its ink, so indented, centred,
// justified and tabbed lines keep their places; letters printed apart stand
// that far apart. A line's baseline is where it stands at its first
// character, sloping by the given tangent. Empty where a line has no fit.
QList<TextLayout::OriginalLine> placedLines(const QList<TextLayout::OriginalLine> &texts,
                                            const ScanFont::Match &face, double slope, double spacing,
                                            QList<TextLayout::Mark> *marks)
{
    constexpr int kMeasurePx = 1000;
    // Each style of the face, measured where a word printed in it is placed.
    const auto metricsOf = [&](bool bold, bool italic) {
        QFont font(face.family);
        font.setPixelSize(kMeasurePx);
        font.setBold(bold);
        font.setItalic(italic);
        font.setKerning(face.kerned);
        font.setHintingPreference(QFont::PreferNoHinting);
        font.setStyleStrategy(QFont::NoFontMerging);
        return QFontMetricsF(font);
    };
    const QFontMetricsF styles[4] = { metricsOf(false, false), metricsOf(true, false),
                                      metricsOf(false, true), metricsOf(true, true) };
    const int plain = (face.bold ? 1 : 0) + (face.italic ? 2 : 0);
    const QFontMetricsF &fm = styles[plain];
    const double k = face.sizePt / kMeasurePx;
    const double space = fm.horizontalAdvance(u' ') * k;
    QList<int> styleOf;
    // How far the pen goes over some letters, and where their ink ends: the spacing follows each but the last.
    const auto advance = [&](int j, const QString &text) {
        return styles[styleOf.at(j)].horizontalAdvance(text) * k + text.size() * spacing;
    };
    const auto inkEnd = [&](int j, const QString &text) { return advance(j, text) - (text.isEmpty() ? 0.0 : spacing); };
    const auto penFor = [&](int j, const QString &text, double middle) {
        const QRectF tight = styles[styleOf.at(j)].tightBoundingRect(text);
        return middle - tight.left() * k - (tight.width() * k + (text.size() - 1) * spacing) / 2.0;
    };
    marks->clear();
    QList<TextLayout::OriginalLine> out;
    for (int i = 0; i < texts.size(); ++i) {
        const ScanFont::Fit &fit = face.lines.at(i);
        if (fit.sizePt <= 0.0) return {};
        TextLayout::OriginalLine line;
        line.rect     = texts.at(i).rect;
        line.text     = texts.at(i).text;
        line.baseline = fit.originPt.y();
        QList<std::pair<int, QString>> words;
        for (int c = 0; c < line.text.size();) {
            if (line.text.at(c).isSpace()) { ++c; continue; }
            int e = c;
            while (e < line.text.size() && !line.text.at(e).isSpace()) ++e;
            words.append({ c, line.text.mid(c, e - c) });
            c = e;
        }
        if (words.isEmpty()) return {};

        // The words the fit found apart are placed where it found them; a
        // line it took whole is placed whole.
        QList<double> pens(words.size(), qQNaN());
        // The word of the line each word of the fit starts at: one of the fit
        // may be two of the line that OCR read apart where no gap is.
        QList<std::pair<int, int>> wordOf;
        for (int o = 0; wordOf.isEmpty() && !fit.words.isEmpty() && o < words.size(); ++o) {
            QList<std::pair<int, int>> of;
            for (int j = 0, w = o; j < fit.words.size() && w < words.size(); ++j) {
                const QString &want = fit.words.at(j);
                if (words.at(w).second == want || (isMark(words.at(w).second) && want.size() <= 2)) {
                    of.append({ w, w });
                    ++w;
                } else if (w + 1 < words.size() && words.at(w).second + words.at(w + 1).second == want) {
                    of.append({ w, w + 1 });
                    w += 2;
                } else {
                    break;
                }
            }
            if (of.size() == fit.words.size()) wordOf = of;
        }
        const bool apart = !wordOf.isEmpty() && fit.words.size() == fit.wordMiddlesPt.size();
        // A word printed bold or italic in a line set otherwise keeps its style; a
        // whole line's weight is the paragraph's, told on all its lines together,
        // unless the line's own is clear, as of one mostly in a bold phrase.
        styleOf = QList<int>(words.size(), plain);
        const bool clear = std::abs(fit.boldMargin) > 0.1;
        if (apart && fit.wordBold.size() == fit.words.size() && fit.wordItalic.size() == fit.words.size())
            for (int j = 0; j < fit.words.size(); ++j) {
                const bool bold = fit.wordBold.at(j) != fit.bold || clear ? fit.wordBold.at(j) : face.bold;
                for (int w = wordOf.at(j).first; w <= wordOf.at(j).second; ++w)
                    styleOf[w] = (bold ? 1 : 0) + (fit.wordItalic.at(j) ? 2 : 0);
            }
        // Words taken together stand where the text layer put them.
        if (apart) {
            for (int j = 0; j < fit.words.size(); ++j)
                if (const auto [w, last] = wordOf.at(j); w == last)
                    pens[w] = penFor(w, words.at(w).second, fit.wordMiddlesPt.at(j));
        }
        // Other words start where the text layer put their first character,
        // which OCR found at its ink.
        const QList<double> &layer = texts.at(i).charX;
        if (layer.size() == line.text.size()) {
            for (int j = 0; j < words.size(); ++j)
                if (std::isnan(pens.at(j)))
                    pens[j] = layer.at(words.at(j).first) - styles[styleOf.at(j)].tightBoundingRect(words.at(j).second).left() * k;
        } else if (!apart && fit.wordMiddlesPt.size() == 1 && fit.words.value(0) == line.text.simplified()) {
            pens[0] = penFor(0, fit.words.first(), fit.wordMiddlesPt.first());
        } else if (!apart) {
            pens[0] = fit.originPt.x();
        }
        // Words without a place follow the one before at a space, never on top of it.
        int first = 0;
        while (std::isnan(pens.at(first))) ++first;
        // Letters set apart stand so across a space too: the word's last letter and the space each take the spacing.
        const double between = space + 2.0 * spacing;
        for (int j = first - 1; j >= 0; --j) pens[j] = pens.at(j + 1) - between - inkEnd(j, words.at(j).second);
        for (int j = first + 1; j < words.size(); ++j) {
            const double end = pens.at(j - 1) + inkEnd(j - 1, words.at(j - 1).second);
            pens[j] = std::isnan(pens.at(j)) ? end + between : qMax(pens.at(j), end + space * 0.25);
        }

        line.charX.reserve(line.text.size());
        int w = 0;
        double end = pens.first();
        for (int c = 0; c < line.text.size(); ++c) {
            while (w < words.size() && c >= words.at(w).first + words.at(w).second.size()) {
                end = pens.at(w) + inkEnd(w, words.at(w).second);
                ++w;
            }
            TextLayout::Mark mark;
            if (w < words.size() && c >= words.at(w).first) {
                line.charX.append(pens.at(w) + advance(w, words.at(w).second.left(c - words.at(w).first)));
                if ((styleOf.at(w) & 1) != (plain & 1)) mark.bold = (styleOf.at(w) & 1) != 0;
                if ((styleOf.at(w) & 2) != (plain & 2)) mark.italic = (styleOf.at(w) & 2) != 0;
            } else {
                line.charX.append(w == 0 ? pens.first() : end);
            }
            marks->append(mark);
        }
        if (i + 1 < texts.size()) marks->append(TextLayout::Mark{});
        line.endX        = pens.last() + inkEnd(int(words.size()) - 1, words.last().second);
        line.objectStart = QList<bool>(line.text.size(), false);
        line.baseline = scanStand(fit, slope) + slope * line.charX.first();
        out.append(line);
    }
    return out;
}

}

namespace {

// Words the fit set beside their ink are set each over its own, from its first run to its last,
// where the line parts into its words by gaps clearly wider than any inside them.
bool wordsOnInk(TextLayout::OriginalLine &line, const QList<std::pair<double, double>> &runs, const QFontMetricsF &fm,
                double k, double size)
{
    QList<std::pair<int, int>> words;
    for (int c = 0; c < line.text.size();) {
        if (line.text.at(c).isSpace()) { ++c; continue; }
        int e = c;
        while (e < line.text.size() && !line.text.at(e).isSpace()) ++e;
        words.append({ c, e });
        c = e;
    }
    if (words.isEmpty() || runs.size() < words.size()) return false;
    QList<double> gaps;
    for (int r = 1; r < runs.size(); ++r) gaps.append(runs.at(r).first - runs.at(r - 1).second);
    std::sort(gaps.begin(), gaps.end(), std::greater<double>());
    const int between = int(words.size()) - 1;
    const double parts = between > 0 ? gaps.at(between - 1) : size;
    if (parts < size * 0.12 || (between < gaps.size() && parts < gaps.at(between) * 1.4)) return false;
    QList<std::pair<double, double>> groups;
    for (const auto &run : runs) {
        if (!groups.isEmpty() && run.first - groups.last().second < parts - 1e-6) groups.last().second = run.second;
        else groups.append(run);
    }
    if (groups.size() != words.size()) return false;
    QList<double> pens, scales;
    double off = 0.0;
    for (int j = 0; j < words.size(); ++j) {
        const auto [a, b] = words.at(j);
        const QRectF tight = fm.tightBoundingRect(line.text.mid(a, b - a));
        if (tight.width() <= 0.0) return false;
        scales.append((groups.at(j).second - groups.at(j).first) / (tight.width() * k));
        pens.append(groups.at(j).first - tight.left() * k * scales.last());
        if (scales.last() < 0.8 || scales.last() > 1.25 || std::abs(pens.last() - line.charX.at(a)) > size * 0.8) return false;
        const double left = line.charX.at(a) + fm.tightBoundingRect(QString(line.text.at(a))).left() * k;
        const double right = line.charX.at(b - 1) + fm.tightBoundingRect(QString(line.text.at(b - 1))).right() * k;
        off = qMax(off, qMax(std::abs(left - groups.at(j).first), std::abs(right - groups.at(j).second)));
    }
    if (off < size * 0.2) return false;
    for (int j = 0; j < words.size(); ++j) {
        const auto [a, b] = words.at(j);
        const QString word = line.text.mid(a, b - a);
        for (int c = a; c < b; ++c) line.charX[c] = pens.at(j) + fm.horizontalAdvance(word.left(c - a)) * k * scales.at(j);
        const double end = pens.at(j) + fm.horizontalAdvance(word) * k * scales.at(j);
        for (int c = b; c < line.text.size() && line.text.at(c).isSpace(); ++c)
            line.charX[c] = j + 1 < words.size() ? qMin(end, pens.at(j + 1)) : end;
        if (j + 1 == words.size()) line.endX = end;
    }
    for (int c = 0; c < words.first().first; ++c) line.charX[c] = pens.first();
    return true;
}

}

void EditOpener::settleScanLetters(EditOpen &o, const ScanFont::Match &face)
{
    // Letters that stand apart, as figures do, are set each on its own ink: a
    // line showing as many runs of ink as it has letters, each about as wide
    // as its letter or near where the face put it. What the face's widths add
    // up to along a line is off by a letter where OCR read spaces that are none.
    QFont font(face.family);
    font.setPixelSize(1000);
    font.setBold(face.bold);
    font.setItalic(face.italic);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::NoFontMerging);
    const QFontMetricsF fm(font);
    const double k = face.sizePt / 1000.0, size = face.sizePt;
    QList<double> sizes;
    const bool fitted = face.lines.size() == o.scanOriginals.size();
    for (qsizetype at = 0; at < o.scanOriginals.size(); ++at) {
        TextLayout::OriginalLine &line = o.scanOriginals[at];
        QList<int> letters;
        for (int c = 0; c < line.text.size(); ++c)
            if (!line.text.at(c).isSpace()) letters.append(c);
        if (letters.size() < 2 || line.charX.size() != line.text.size()) continue;
        const double first = line.charX.first();
        // As far as the line's ink reaches: the face may have set its letters short of where the scan has them.
        const double left = qMin(first, line.rect.left()) - size * 0.3, right = qMax(line.endX, line.rect.right()) + size * 0.3;
        const double a = line.baseline + o.scanSlope * (left - first), b = line.baseline + o.scanSlope * (right - first);
        QList<std::pair<double, double>> runs;
        double from = left;
        for (const auto &[start, end] : o.gaps(QRectF(QPointF(left, qMin(a, b) - size * 0.78), QPointF(right, qMax(a, b) + size * 0.22)), o.scanSlope)) {
            if (start > from + 1e-6) runs.append({ from, start });
            from = end;
        }
        if (from < right - 1e-6) runs.append({ from, right });
        QList<double> pens;
        bool wide = runs.size() == letters.size(), near = wide;
        // Letters with white as wide as a quarter of their size between each two of them stand apart: as many
        // runs of ink as letters are those letters, whatever widths the matched face gives them.
        bool apart = wide;
        for (int n = 1; n < runs.size() && apart; ++n) apart = runs.at(n).first - runs.at(n - 1).second >= size * 0.25;
        for (int n = 0; n < letters.size() && runs.size() == letters.size(); ++n) {
            const QRectF tight = fm.tightBoundingRect(QString(line.text.at(letters.at(n))));
            pens.append((runs.at(n).first + runs.at(n).second) / 2.0 - (tight.left() + tight.width() / 2.0) * k);
            const double width = runs.at(n).second - runs.at(n).first;
            wide = wide && std::abs(width - tight.width() * k) <= size * 0.15 + tight.width() * k * 0.3;
            near = near && std::abs(pens.last() - line.charX.at(letters.at(n))) <= size * 0.45;
        }
        if (!wide && !near && !apart) {
            if (wordsOnInk(line, runs, fm, k, size)) line.baseline += o.scanSlope * (line.charX.first() - first);
            continue;
        }
        for (int n = 0; n < letters.size(); ++n) {
            const int c = letters.at(n);
            line.charX[c] = pens.at(n);
            // A space starts where the letter before it ends.
            const double end = pens.at(n) + fm.horizontalAdvance(line.text.at(c)) * k;
            for (int s = c + 1; s < line.text.size() && line.text.at(s).isSpace(); ++s)
                line.charX[s] = n + 1 < letters.size() ? qMin(end, pens.at(n + 1)) : end;
            if (n + 1 == letters.size()) line.endX = end;
        }
        for (int s = 0; s < letters.first(); ++s) line.charX[s] = pens.first();
        line.baseline += o.scanSlope * (line.charX.first() - first);
        // Each letter alone tells the size by how high its ink stands against the face's letter, where
        // the letters are printed apart: the widths along such a line tell it badly, those of plain text well.
        QList<double> gaps;
        for (int n = 1; n < runs.size(); ++n) gaps.append(runs.at(n).first - runs.at(n - 1).second);
        std::sort(gaps.begin(), gaps.end());
        // A line the face was fitted to by its height, blurred as the scan, has its size: the ink here shows a blurred stroke wider.
        if (gaps.at(gaps.size() / 2) < size * 0.2 || (fitted && face.lines.at(at).byHeight)) continue;
        for (int n = 0; n < letters.size(); ++n) {
            const double high = fm.tightBoundingRect(QString(line.text.at(letters.at(n)))).height();
            const double base = line.baseline + o.scanSlope * (runs.at(n).first - line.charX.first());
            const QRectF ink = o.inkOnBaseline(QRectF(runs.at(n).first, base - size, runs.at(n).second - runs.at(n).first, size * 1.3), base);
            if (high > 300.0 && ink.height() > 2.0) sizes.append(ink.height() / high * 1000.0);
        }
    }
    // Where enough of them tell another size than the widths along the line did, theirs it is.
    std::sort(sizes.begin(), sizes.end());
    if (sizes.size() >= 3 && std::abs(sizes.at(sizes.size() / 2) - size) > size * 0.06)
        o.fontSizePt = qRound(sizes.at(sizes.size() / 2) * 2.0) / 2.0;
}

bool EditOpener::placeScanLines(EditOpen &o, QList<TextLayout::OriginalLine> &texts, const ScanFont::Match &face)
{
    // A page lying askew by less than a few degrees: its lines are set level in a box turned with them.
    const double slope = scanSlope(face.lines);
    o.scanSlope = std::abs(qRadiansToDegrees(std::atan(slope))) <= kScanAskewDeg ? slope : 0.0;
    // Read again at the size and on the baselines the face was matched at, which a page lying askew needs.
    ScanCleanup::readMarks(texts, [&](int i, double x) {
        const ScanFont::Fit &fit = face.lines.at(i);
        return fit.sizePt > 0.0 ? std::make_pair(face.sizePt, scanStand(fit, o.scanSlope) + o.scanSlope * x)
                                : std::make_pair(0.0, 0.0);
    }, [&o](const QRectF &pt) { return o.blob(pt); });
    QList<TextLayout::Mark> marks;
    o.scanOriginals = placedLines(texts, face, o.scanSlope, o.box.characterSpacingPt, &marks);
    settleScanLetters(o, face);
    o.scanMarks.clear();
    if (std::any_of(marks.cbegin(), marks.cend(), [](const TextLayout::Mark &m) { return !m.isEmpty(); }))
        o.scanMarks = marks;
    o.scanStands.clear();
    o.scanWords.clear();
    for (const TextLayout::OriginalLine &line : std::as_const(o.scanOriginals)) {
        o.scanStands.append(line.baseline - o.scanSlope * line.charX.first());
        // Each word from where it starts to where the space after it does, as set on the page.
        QList<std::pair<double, double>> words;
        for (qsizetype c = 0; c < line.text.size(); ++c) {
            if (line.text.at(c).isSpace() || (c > 0 && !line.text.at(c - 1).isSpace())) continue;
            qsizetype e = c;
            while (e < line.text.size() && !line.text.at(e).isSpace()) ++e;
            words.append({ line.charX.at(c), e < line.charX.size() ? line.charX.at(e) : line.endX });
        }
        o.scanWords.append(words);
    }
    return !o.scanOriginals.isEmpty();
}

void EditOpener::alignScan(EditOpen &o)
{
    // Lines of a scan set flush right or centred grow that way: told by their
    // own edges where there are several, else by the lines of the page whose
    // edges they share. Their box takes the room beside them they grow into;
    // to their right they have room up to where the page's text ends or other
    // text stands beside them.
    const QList<TextLayout::OriginalLine> &own = o.isSessionEdit ? o.sessionEdit.originalLines : o.scanOriginals;
    if (!o.scanned || own.isEmpty() || own.first().charX.isEmpty() || !m_src->backend()) return;
    constexpr double kSame = 1.2;
    double leftLow = 1e9, leftHigh = -1e9, rightLow = 1e9, rightHigh = -1e9, midLow = 1e9, midHigh = -1e9;
    QRectF unit;
    for (const TextLayout::OriginalLine &line : own) {
        if (line.charX.isEmpty()) continue;
        const double left = line.charX.first(), right = line.endX, mid = (left + right) / 2.0;
        leftLow = qMin(leftLow, left); leftHigh = qMax(leftHigh, left);
        rightLow = qMin(rightLow, right); rightHigh = qMax(rightHigh, right);
        midLow = qMin(midLow, mid); midHigh = qMax(midHigh, mid);
        unit = unit.united(line.rect);
    }
    const bool several = own.size() >= 2;
    const bool lefts = leftHigh - leftLow <= kSame, rights = rightHigh - rightLow <= kSame, mids = midHigh - midLow <= kSame;
    using Align = TextBoxProperties::HorizontalAlign;
    Align align = Align::Left;
    // OCR may read a space behind the decimal point of a face that sets it wide, and a speck behind the amount as a stop.
    static const QRegularExpression amount(QStringLiteral("^[+\\-\u2212]?\\s?\\d[\\d.' ]*,\\s?\\d{2}(\\s?(\u20ac|EUR))?[.,]?$|^[+\\-\u2212]?\\s?\\d[\\d,' ]*\\.\\s?\\d{2}(\\s?(\u20ac|EUR|\\$))?[.,]?$"));
    // The lines beside the box on its rows end its room; those of the page tell its margins.
    double pageLeft = 1e9, pageRight = -1e9, room = 0.0, beside = 1e9;
    int sameLeft = 0, sameRight = 0, sameRightNear = 0, sameBoth = 0;
    QList<TextLayout::OriginalLine> others = m_src->backend()->originalLines(o.block.page, QRectF(QPointF(), o.pageSize), o.erasedZones);
    // The lines close by are compared by their ink: a box read with a rule beside its text is wider than the text.
    QList<TextLayout::OriginalLine> near;
    for (qsizetype i = others.size() - 1; i >= 0; --i)
        if (std::abs(others.at(i).rect.center().y() - unit.center().y()) <= unit.height() / 2.0 + o.fontSizePt * 6.0)
            near.append(others.takeAt(i));
    o.narrowToInk(near);
    others += near;
    // How far before a line its ink is looked for: further where its letters stand apart, a step and more from each other.
    const double before = o.fontSizePt * 1.6 + 2.0 * o.box.characterSpacingPt;
    for (const TextLayout::OriginalLine &other : std::as_const(others)) {
        if (other.rect.isEmpty() || other.rect.intersects(unit.adjusted(1.0, 1.0, -1.0, -1.0))) continue;
        pageLeft = qMin(pageLeft, other.rect.left());
        pageRight = qMax(pageRight, other.rect.right());
        // Ink against ink: where a line's pen starts and ends lies beside its ink by the face's bearings.
        // Sharing an edge tells only with a line of another width: lines as wide as each other share both,
        // as the figures of a column do.
        const bool wide = std::abs(other.rect.width() - unit.width()) > qMax(o.fontSizePt * 0.6, unit.width() * 0.08);
        const bool left = std::abs(other.rect.left() - unit.left()) <= kSame * 2.0;
        // A line with ink close before it on its rows is the end of something longer, and its edge tells nothing.
        const bool right = std::abs(other.rect.right() - unit.right()) <= kSame * 2.0 && wide
                        && !o.inkBefore(other.rect, before);
        sameLeft += left && wide;
        sameBoth += left && !wide && std::abs(other.rect.right() - unit.right()) <= kSame * 2.0;
        sameRight += right;
        sameRightNear += right && std::abs(other.rect.center().y() - unit.center().y()) <= unit.height() / 2.0 + o.fontSizePt * 2.5;
        if (other.rect.bottom() <= unit.top() || other.rect.top() >= unit.bottom()) continue;
        if (other.rect.right() <= leftLow) room = qMax(room, other.rect.right() + o.fontSizePt * 0.5);
        if (other.rect.left() >= rightHigh) beside = qMin(beside, other.rect.left() - o.fontSizePt * 0.5);
    }
    if (pageRight > pageLeft) {
        // A short entry alone that ends at the page's text edge, as in a table's last column, runs on
        // towards the page's edge instead of breaking at once; any other line flows at the text's edge.
        const bool entry = !several && unit.width() < (pageRight - pageLeft) * 0.5 && rightHigh > pageRight - o.fontSizePt * 2.0;
        const double edge = entry ? qMax(pageRight, o.pageSize.width() - qMax(12.0, pageLeft * 0.5)) : pageRight;
        o.roomRight = qMax(qMin(qMax(edge, rightHigh), beside), rightHigh) + 1.0;
    }
    if (o.isSessionEdit) return;
    // A line alone with ink close before it on its rows is the end of something longer, as figures behind
    // a smudge: it shares its right edge with the rows around it and still grows to the right.
    if (!several && o.inkBefore(unit, before)) return;
    if (several && !lefts && rights) align = Align::Right;
    else if (several && !lefts && !rights && mids) align = Align::Center;
    else if ((!several || (lefts && rights)) && sameLeft == 0 && (sameRight >= 2 || sameRightNear >= 1)) align = Align::Right;
    // An amount of money in a column of amounts as wide as it stands flush right, as its decimals stand below each other.
    else if (!several && sameLeft == 0 && sameBoth >= 1 && amount.match(own.first().text.trimmed()).hasMatch()) align = Align::Right;
    else if (!several && sameLeft == 0 && sameRight == 0 && sameBoth == 0 && pageRight > pageLeft
             && std::abs((leftLow + rightHigh) / 2.0 - (pageLeft + pageRight) / 2.0) <= 2.0
             && leftLow - pageLeft > o.fontSizePt * 3.0) align = Align::Center;
    if (align == Align::Left) return;
    o.box.horizontalAlign = align;
    const double reach = align == Align::Right ? qMax(60.0, unit.width()) : qMax(30.0, unit.width() / 2.0);
    o.bounds.setLeft(qMin(o.bounds.left(), qMax(qMax(room, 2.0), leftLow - reach)));
}

void EditOpener::levelScan(EditOpen &o)
{
    // The lines of a scan lying askew are laid out level in a box turned
    // about its middle by the scan's slope; each keeps the places of its words.
    const double degrees = qRadiansToDegrees(std::atan(o.scanSlope));
    if (o.scanOriginals.isEmpty() || std::abs(degrees) < 0.02) return;
    const QPointF c = o.bounds.center();
    const QTransform level = QTransform().translate(c.x(), c.y()).rotate(-degrees).translate(-c.x(), -c.y());
    for (TextLayout::OriginalLine &line : o.scanOriginals) {
        const double x0 = line.charX.first(), base = line.baseline;
        const auto at = [&](double x) { return level.map(QPointF(x, base + o.scanSlope * (x - x0))); };
        line.baseline = at(x0).y();
        for (double &x : line.charX) x = at(x).x();
        line.endX = at(line.endX).x();
        o.bounds.setRight(qMax(o.bounds.right(), line.endX + 0.5));
    }
    o.textOrigin = QPointF(o.scanOriginals.first().charX.first(), o.scanOriginals.first().baseline);
    o.box.rotationDeg = degrees;
    // The box grows with its text; what it turns about stays where the lines were levelled.
    o.turnPivot = c;
}

void EditOpener::measureScan(EditOpen &o)
{
    // The scanned ink of each line taken is painted over with the paper.
    if (!o.scanned || o.isSessionEdit) return;
    o.scanRects.clear();
    // Lines set where the scan's words are cover just those lines; one
    // sloping across the page is covered piece by piece along its slope, so
    // the cover keeps off the lines above and below.
    // Each word on its own, parted from the next in the middle of the white
    // between them, so a word an edit keeps keeps every pixel of its ink.
    if (!o.scanOriginals.isEmpty() && o.scanStands.size() == o.scanOriginals.size()
            && o.scanWords.size() == o.scanOriginals.size()) {
        const double fs = o.fontSizePt;
        for (int i = 0; i < o.scanOriginals.size(); ++i) {
            const QList<std::pair<double, double>> &words = o.scanWords.at(i);
            const auto baseAt = [&](double x) { return o.scanStands.at(i) + o.scanSlope * x; };
            QList<double> cuts;
            for (int w = 1; w < words.size(); ++w) {
                // Between the middles of the two words at most, so a mark is not passed over.
                const double from = qMax(words.at(w - 1).second - fs * 0.4, (words.at(w - 1).first + words.at(w - 1).second) / 2.0);
                const double to = qMin(words.at(w).first + fs * 0.4, (words.at(w).first + words.at(w).second) / 2.0);
                const double base = baseAt((from + to) / 2.0);
                const double gap = to > from ? o.gapMiddle(QRectF(from, base - fs * 0.75, to - from, fs * 0.75)) : qQNaN();
                cuts.append(std::isnan(gap) ? (words.at(w - 1).second + words.at(w).first) / 2.0 : gap);
            }
            // The line's ends are parted from what stands beside it in the white
            // there, as nothing beside it belongs to the edit.
            const auto end = [&](double from, double to, double otherwise) {
                const double base = baseAt((from + to) / 2.0);
                const double gap = o.gapMiddle(QRectF(from, base - fs * 0.75, to - from, fs * 0.75));
                return std::isnan(gap) ? otherwise : gap;
            };
            const QRectF line = o.scanOriginals.at(i).rect;
            const double first = words.isEmpty() ? 0.0 : end(words.first().first - fs * 0.6, words.first().first + fs * 0.1,
                                                            qMin(words.first().first, line.left()) - 0.75);
            const double last = words.isEmpty() ? 0.0 : end(words.last().second - fs * 0.1, words.last().second + fs * 0.6,
                                                           qMax(words.last().second, line.right()) + 0.75);
            const QStringList texts = o.scanOriginals.at(i).text.split(u' ', Qt::SkipEmptyParts);
            for (int w = 0; w < words.size(); ++w) {
                const double left = w == 0 ? first : cuts.at(w - 1);
                const double right = w + 1 == words.size() ? last : cuts.at(w);
                if (right <= left) continue;
                const double base = baseAt((left + right) / 2.0);
                // A mark read as a word of its own, as a colon, may stand in two parts or off the baseline:
                // it has the room of a letter, not that of the part found on the baseline.
                const QString text = texts.size() == words.size() ? texts.at(w) : QString();
                const bool mark = !text.isEmpty() && std::none_of(text.cbegin(), text.cend(), [](QChar c) { return c.isLetterOrNumber(); });
                QRectF ink = mark ? QRectF() : o.inkOnBaseline(QRectF(left, base - fs, right - left, fs * 1.3), base);
                if (ink.isEmpty()) ink = QRectF(QPointF(left, base - fs * 0.75), QPointF(right, base + fs * 0.25));
                o.scanRects.append(QRectF(QPointF(left, ink.top() - 0.75), QPointF(right, ink.bottom() + 0.75)));
            }
        }
        return;
    }
    if (!o.scanOriginals.isEmpty() && o.scanStands.size() == o.scanOriginals.size()) {
        for (int i = 0; i < o.scanOriginals.size(); ++i) {
            const QRectF line = o.scanOriginals.at(i).rect;
            if (std::abs(o.scanSlope) * line.width() < 0.5) {
                o.scanRects.append(line.adjusted(-0.75, -0.75, 0.75, 0.75));
                continue;
            }
            const double piece = qMax(o.fontSizePt * 2.0, 0.5 / std::abs(o.scanSlope));
            for (double x = line.left(); x < line.right(); x += piece) {
                const double right = qMin(line.right(), x + piece);
                const double base = o.scanStands.at(i) + o.scanSlope * (x + right) / 2.0;
                const QRectF ink = o.inkOnBaseline(QRectF(x, base - o.fontSizePt, right - x, o.fontSizePt * 1.3), base);
                if (!ink.isEmpty()) o.scanRects.append(ink.adjusted(-0.75, -0.75, 0.75, 0.75));
            }
        }
        return;
    }
    const QList<QRectF> lines = scanLines(o);
    for (const QRectF &line : std::as_const(lines)) {
        // Lines read are already their ink; a frame takes its characters only, not the words beside them.
        QRectF ink = line;
        if (!o.framedLines.isEmpty()) {
            ink = o.inkAround(line);
            ink.setLeft(qMax(ink.left(), line.left() - 0.5));
            ink.setRight(qMin(ink.right(), line.right() + 0.5));
        }
        o.scanRects.append(ink.adjusted(-0.75, -0.75, 0.75, 0.75));
    }
}

#endif
