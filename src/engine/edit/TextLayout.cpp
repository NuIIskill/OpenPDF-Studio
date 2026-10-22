#include "engine/edit/TextLayout.hpp"

#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

constexpr double kTolerance = 0.05;
constexpr qint64 kMaxCompared = 2'000'000;

struct Range {
    int start  { 0 };
    int length { 0 };
};

struct Glyph {
    int      index;
    char32_t codePoint;
};

QList<Glyph> glyphsOf(const QString &text, int from, int to)
{
    QList<Glyph> out;
    for (int i = from; i < to;) {
        char32_t cp = text.at(i).unicode();
        int len = 1;
        if (text.at(i).isHighSurrogate() && i + 1 < to && text.at(i + 1).isLowSurrogate()) {
            cp  = QChar::surrogateToUcs4(text.at(i), text.at(i + 1));
            len = 2;
        }
        out.append({ i, cp });
        i += len;
    }
    return out;
}

double widthOf(const QString &text, int from, int to, const TextLayout::Advance &advance,
               double charSpacing, bool marker = false)
{
    while (to > from && text.at(to - 1).isSpace()) --to;
    const QList<Glyph> glyphs = glyphsOf(text, from, to);
    double width = 0.0;
    for (const Glyph &g : glyphs) width += advance(marker ? -1 : g.index, g.codePoint);
    if (glyphs.size() > 1) width += charSpacing * (glyphs.size() - 1);
    return width;
}

QList<Range> wrap(const QString &text, int from, int to, double maxWidth,
                  const TextLayout::Advance &advance, double charSpacing)
{
    const QList<Glyph> glyphs = glyphsOf(text, from, to);
    if (glyphs.isEmpty()) return { { from, 0 } };

    QList<double> bis { 0.0 };
    double breite = 0.0;
    for (int i = 0; i < glyphs.size(); ++i) {
        breite += advance(glyphs.at(i).index, glyphs.at(i).codePoint);
        if (i > 0) breite += charSpacing;
        bis.append(breite);
    }
    if (widthOf(text, from, to, advance, charSpacing) <= maxWidth + kTolerance)
        return { { from, to - from } };

    const auto at = [&](int glyph) { return glyph < glyphs.size() ? glyphs.at(glyph).index : to; };
    QList<Range> out;
    int start = 0;
    while (start < glyphs.size()) {
        int ende = start;
        while (ende < glyphs.size()
               && bis.at(ende + 1) - bis.at(start) <= maxWidth + kTolerance)
            ++ende;
        if (ende <= start) ende = start + 1;
        if (ende < glyphs.size()) {
            int leer = QChar::isSpace(glyphs.at(ende).codePoint) ? ende : -1;
            for (int i = ende - 1; leer < 0 && i > start; --i)
                if (QChar::isSpace(glyphs.at(i).codePoint)) leer = i;
            if (leer > start) {
                out.append({ at(start), at(leer) - at(start) });
                start = leer + 1;
                while (start < glyphs.size() && QChar::isSpace(glyphs.at(start).codePoint))
                    ++start;
                continue;
            }
        }
        out.append({ at(start), at(ende) - at(start) });
        start = ende;
    }
    return out;
}

QList<int> matchOriginal(const QString &text, const QList<int> &at, const QString &original)
{
    const int n = int(at.size()), m = int(original.size());
    QList<int> source(n, -1);
    int head = 0;
    while (head < n && head < m && text.at(at[head]) == original.at(head)) {
        source[head] = head;
        ++head;
    }
    int tail = 0;
    while (tail < n - head && tail < m - head
           && text.at(at[n - 1 - tail]) == original.at(m - 1 - tail)) {
        source[n - 1 - tail] = m - 1 - tail;
        ++tail;
    }
    const int rows = n - head - tail, cols = m - head - tail;
    if (rows <= 0 || cols <= 0 || qint64(rows) * cols > kMaxCompared) return source;

    std::vector<int> common(size_t(rows + 1) * size_t(cols + 1), 0);
    const auto cell = [&](int r, int c) -> int & { return common[size_t(r) * size_t(cols + 1) + size_t(c)]; };
    for (int r = rows - 1; r >= 0; --r)
        for (int c = cols - 1; c >= 0; --c)
            cell(r, c) = text.at(at[head + r]) == original.at(head + c)
                ? cell(r + 1, c + 1) + 1 : qMax(cell(r + 1, c), cell(r, c + 1));
    for (int r = 0, c = 0; r < rows && c < cols;) {
        if (text.at(at[head + r]) == original.at(head + c)) {
            source[head + r] = head + c;
            ++r;
            ++c;
        } else if (cell(r + 1, c) >= cell(r, c + 1)) {
            ++r;
        } else {
            ++c;
        }
    }
    // A letter or two that happens to match somewhere is typed text, not a
    // piece of the original; only runs of unchanged text keep their source.
    constexpr int kShortest = 3;
    for (int r = head; r < head + rows;) {
        if (source[r] < 0) { ++r; continue; }
        int end = r + 1;
        while (end < head + rows && source[end] == source[end - 1] + 1) ++end;
        const bool touchesKept = (r == head && source[r] == head)
                              || (end == head + rows && source[end - 1] == head + cols - 1);
        if (end - r < kShortest && !touchesKept)
            for (int i = r; i < end; ++i) source[i] = -1;
        r = end;
    }
    return source;
}

}

QString TextLayout::originalText(const QList<OriginalLine> &originals)
{
    QStringList texts;
    for (const OriginalLine &line : originals) texts << line.text;
    return texts.join(QLatin1Char('\n'));
}

QList<int> TextLayout::styleIndices(const QString &text, const QString &original,
                                    const QList<int> &originalStyles)
{
    QList<int> out(text.size(), 0);
    if (original.isEmpty() || originalStyles.size() != original.size()) return out;

    QList<int> at;
    at.reserve(text.size());
    for (int i = 0; i < text.size(); ++i)
        if (text.at(i) != QChar(kSoftBreak)) at.append(i);
    const QList<int> source = matchOriginal(text, at, original);

    QList<int> style(at.size(), -1);
    for (int k = 0; k < at.size(); ++k)
        if (source.at(k) >= 0) style[k] = originalStyles.at(source.at(k));
    // A typed character takes the style of the one before it; at the start
    // of a paragraph it takes the style of the paragraph that follows.
    for (int k = 0; k < at.size();) {
        if (style.at(k) >= 0) { ++k; continue; }
        int end = k;
        while (end < at.size() && style.at(end) < 0) ++end;
        const bool paragraphStart = k == 0 || text.at(at[k - 1]) == QLatin1Char('\n');
        // Typed at the start of a word, as over its first letters, it takes the word's style.
        const bool wordStart = k > 0 && text.at(at[k - 1]).isSpace() && end < at.size()
                            && !text.at(at[end]).isSpace() && !text.at(at[end - 1]).isSpace();
        int inherit = k > 0 ? style.at(k - 1) : -1;
        if ((paragraphStart || wordStart || inherit < 0) && end < at.size()
                && text.at(at[end]) != QLatin1Char('\n'))
            inherit = style.at(end);
        if (inherit < 0) inherit = k > 0 ? style.at(k - 1) : originalStyles.value(0, 0);
        for (int i = k; i < end; ++i) style[i] = inherit;
        k = end;
    }
    for (int k = 0; k < at.size(); ++k) out[at[k]] = style.at(k);
    for (int i = 1; i < text.size(); ++i)
        if (text.at(i) == QChar(kSoftBreak)) out[i] = out[i - 1];
    return out;
}

int TextLayout::dominantStyle(const QList<int> &styles, int count)
{
    if (styles.isEmpty() || count <= 0) return 0;
    QList<int> tally(count, 0);
    for (const int s : styles) ++tally[qBound(0, s, count - 1)];
    return int(std::max_element(tally.cbegin(), tally.cend()) - tally.cbegin());
}

QList<TextLayout::Source> TextLayout::distinctSources(const QList<Source> &sources)
{
    QList<Source> out;
    for (const Source &s : sources)
        if (s.isValid() && !out.contains(s)) out.append(s);
    std::sort(out.begin(), out.end(), [](const Source &a, const Source &b) {
        if (a.page != b.page) return a.page < b.page;
        if (a.at.y() != b.at.y()) return a.at.y() < b.at.y();
        return a.at.x() < b.at.x();
    });
    return out;
}

QList<TextLayout::Source> TextLayout::alignSources(const QString &text, const QList<Source> &plain)
{
    if (plain.isEmpty() || text.size() == plain.size()) return plain;
    // The laid-out text has soft breaks the editor's own text does not.
    QList<Source> out;
    out.reserve(text.size());
    qsizetype j = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == QChar(kSoftBreak) && text.size() - i > plain.size() - j) {
            out.append(Source{});
            continue;
        }
        out.append(plain.value(j++));
    }
    return out;
}

bool TextLayout::Metrics::covers(const QString &text, const QList<Source> &chars) const
{
    if (advances.isEmpty()) return false;
    const QHash<uint, double> &table = advances.constFirst();
    for (const uint cp : text.toUcs4())
        if (!table.contains(cp)) return false;
    for (const Source &s : chars)
        if (s.isValid() && !sources.contains(s)) return false;
    return true;
}

QList<int> TextLayout::Metrics::stylesOf(const QString &text, const QList<Source> &chars) const
{
    QList<int> styles = styleIndices(text, original, originalStyles);
    // A pasted character keeps the style of the one it was copied from.
    for (qsizetype i = 0; i < styles.size() && i < chars.size(); ++i) {
        const qsizetype k = chars.at(i).isValid() ? sources.indexOf(chars.at(i)) : -1;
        if (k >= 0 && k < sourceStyles.size()) styles[i] = sourceStyles.at(k);
    }
    return styles;
}

double TextLayout::Metrics::sizeFor(const QList<int> &styles) const
{
    return sizes.value(dominantStyle(styles, int(sizes.size())), 0.0);
}

TextLayout::Advance TextLayout::Metrics::advanceFor(const QList<int> &styles) const
{
    const QList<QHash<uint, double>> tables = advances;
    return [styles, tables](int position, char32_t codePoint) {
        if (tables.isEmpty()) return 0.0;
        const int style = position >= 0 && position < styles.size() ? styles.at(position)
                                                                       : styles.value(0, 0);
        return tables.at(qBound(0, style, int(tables.size()) - 1)).value(uint(codePoint), 0.0);
    };
}

bool TextLayout::anchors(const TextBoxProperties &box)
{
    return qFuzzyIsNull(box.characterSpacingPt) && qFuzzyIsNull(box.paddingPt)
        && qFuzzyIsNull(box.paragraphSpacingPt) && qFuzzyIsNull(box.lineSpacingMultiplier)
        && box.horizontalAlign == TextBoxProperties::HorizontalAlign::Left
        && box.verticalAlign == TextBoxProperties::VerticalAlign::Top
        && box.listStyle == TextBoxProperties::ListStyle::None && box.indentLevel == 0;
}

QString TextLayout::markerFor(TextBoxProperties::ListStyle list, int paragraph)
{
    switch (list) {
    case TextBoxProperties::ListStyle::Bullets:  return QStringLiteral("• ");
    case TextBoxProperties::ListStyle::Numbered: return QString::number(paragraph + 1)
                                                        + QStringLiteral(". ");
    case TextBoxProperties::ListStyle::None:     break;
    }
    return {};
}

QString TextLayout::measuredCharacters(const QString &text, TextBoxProperties::ListStyle list)
{
    return list == TextBoxProperties::ListStyle::None
        ? text : text + QStringLiteral("•0123456789. ");
}

QList<TextLayout::Line> TextLayout::lay(const QString &text, const Params &params,
                                        const Advance &advance)
{
    QList<Line> out;
    const bool wraps = params.widthPt > 0.0;
    int paragraph = 0;
    int lineIndex = 0;
    int from = 0;
    while (from <= text.size()) {
        int end = text.indexOf(QLatin1Char('\n'), from);
        if (end < 0) end = text.size();

        const QString prefix = markerFor(params.list, paragraph);
        const double prefixWidth = prefix.isEmpty()
            ? 0.0 : widthOf(prefix, 0, prefix.size(), advance, params.charSpacingPt, true)
                    + advance(-1, U' ') + params.charSpacingPt;
        const double maxWidth = wraps ? qMax(0.0, params.widthPt - prefixWidth)
                                      : std::numeric_limits<double>::max();
        bool first = true;
        int segment = from;
        while (segment <= end) {
            int segmentEnd = text.indexOf(QChar(kSoftBreak), segment);
            if (segmentEnd < 0 || segmentEnd > end) segmentEnd = end;
            for (const Range &r : wrap(text, segment, segmentEnd, maxWidth, advance,
                                       params.charSpacingPt)) {
                Line line;
                line.start     = r.start;
                line.length    = r.length;
                line.paragraph = paragraph;
                line.first     = first;
                if (first) {
                    line.prefix      = prefix;
                    line.prefixWidth = prefixWidth;
                }
                line.width = widthOf(text, r.start, r.start + r.length, advance,
                                     params.charSpacingPt);
                double offset = 0.0;
                if (wraps && params.align == TextBoxProperties::HorizontalAlign::Center)
                    offset = qMax(0.0, (maxWidth - line.width) / 2.0);
                else if (wraps && params.align == TextBoxProperties::HorizontalAlign::Right)
                    offset = qMax(0.0, maxWidth - line.width);
                line.x = prefixWidth + offset;
                line.y = lineIndex * params.lineStepPt
                       + paragraph * params.paragraphSpacingPt;
                out.append(line);
                ++lineIndex;
                first = false;
            }
            segment = segmentEnd + 1;
        }
        // Justified: every line of the paragraph but its last fills the box
        // width, the room spread over its spaces.
        if (wraps && params.align == TextBoxProperties::HorizontalAlign::Justify) {
            for (qsizetype k = out.size() - 1; k >= 0 && out.at(k).paragraph == paragraph; --k) {
                Line &line = out[k];
                if (k == out.size() - 1) continue;
                int stop = line.start + line.length;
                while (stop > line.start && text.at(stop - 1) == u' ') --stop;
                const int spaces = int(QStringView(text).mid(line.start, stop - line.start).count(u' '));
                const double used = widthOf(text, line.start, stop, advance, params.charSpacingPt);
                if (spaces > 0 && maxWidth > used) line.spaceExtra = (maxWidth - used) / spaces;
            }
        }
        ++paragraph;
        from = end + 1;
    }
    // A box that grows with its text aligns its lines on the longest one,
    // which is where the grown box ends.
    if (!wraps && (params.align == TextBoxProperties::HorizontalAlign::Center
                   || params.align == TextBoxProperties::HorizontalAlign::Right)) {
        double longest = params.alignWidthPt;
        for (const Line &line : std::as_const(out))
            longest = qMax(longest, line.prefixWidth + line.width);
        for (Line &line : out) {
            const double free = longest - line.prefixWidth - line.width;
            line.alignShift = params.align == TextBoxProperties::HorizontalAlign::Center ? free / 2.0 : free;
            line.x += line.alignShift;
        }
    }
    return out;
}

double TextLayout::lineStep(double sizePt, double lineSpacingPt)
{
    return lineSpacingPt > 0.0 ? lineSpacingPt : sizePt * 1.2;
}

QPointF TextLayout::firstBaseline(const TextBoxProperties &box, const QSizeF &bounds,
                                  double sizePt, const QList<Line> &lines,
                                  bool hasTextOrigin, const QPointF &textOriginOffset)
{
    const bool customInset = box.paddingPt > 0.0
                          || box.verticalAlign != TextBoxProperties::VerticalAlign::Top;
    if (hasTextOrigin && !customInset)
        return textOriginOffset + QPointF(indentPt(box, bounds, sizePt), 0.0);

    const double contentHeight = sizePt + (lines.isEmpty() ? 0.0 : lines.constLast().y);
    const double innerHeight = qMax(0.0, bounds.height() - 2 * box.paddingPt);
    double offset = 0.0;
    if (box.verticalAlign == TextBoxProperties::VerticalAlign::Center)
        offset = qMax(0.0, (innerHeight - contentHeight) / 2.0);
    else if (box.verticalAlign == TextBoxProperties::VerticalAlign::Bottom)
        offset = qMax(0.0, innerHeight - contentHeight);
    return QPointF(box.paddingPt + indentPt(box, bounds, sizePt),
                   box.paddingPt + offset + sizePt * 0.8);
}

double TextLayout::indentPt(const TextBoxProperties &box, const QSizeF &bounds, double sizePt)
{
    // An indent wider than the box would push the text out of it; one em of
    // the box stays free for the text.
    const double room = bounds.width() - 2 * box.paddingPt - qMax(1.0, sizePt);
    return qBound(0.0, box.indentLevel * 18.0, qMax(0.0, room));
}

double TextLayout::textWidth(const TextBoxProperties &box, const QSizeF &bounds,
                             const QPointF &firstBaseline)
{
    return qMax(0.0, bounds.width() - box.paddingPt - firstBaseline.x());
}

int TextLayout::commonPrefix(const OriginalLine &original, QStringView line)
{
    int common = 0;
    while (common < original.text.size() && common < line.size()
           && original.text.at(common) == line.at(common))
        ++common;
    if (common > 0 && common < line.size() && line.at(common - 1).isHighSurrogate()) --common;
    return common;
}

int TextLayout::commonSuffix(const OriginalLine &original, QStringView line, int prefix)
{
    if (original.charX.size() != original.text.size()) return 0;
    const int room = int(qMin(original.text.size(), line.size())) - prefix;
    int common = 0;
    while (common < room && original.text.at(original.text.size() - 1 - common)
                                == line.at(line.size() - 1 - common))
        ++common;
    // Never the second half of a pair.
    if (common > 0 && line.at(line.size() - common).isLowSurrogate()) --common;
    return qMax(0, common);
}

int TextLayout::keptPrefix(const OriginalLine &original, QStringView line)
{
    const int common = commonPrefix(original, line);
    int kept = 0;
    // A line cut out of a text object has no object start and keeps nothing.
    if (common >= original.text.size() && original.objectStart.value(0)) {
        kept = int(original.text.size());
    } else {
        for (int i = 0; i <= common && i < original.objectStart.size(); ++i)
            if (original.objectStart.at(i)) kept = i;
    }
    // A new run that starts with a space would be read back without it, so
    // kept objects are set again until the new part starts with ink.
    while (kept > 0 && kept < line.size() && line.at(kept).isSpace()) {
        int previous = 0;
        for (int i = 0; i < kept && i < original.objectStart.size(); ++i)
            if (original.objectStart.at(i)) previous = i;
        kept = previous;
    }
    return kept;
}

double TextLayout::restartX(const OriginalLine &original, int kept)
{
    if (kept < original.charX.size()) return original.charX.at(kept);
    return original.endX;
}

TextLayout::Result TextLayout::layout(const QString &text, const Params &params,
                                      const TextBoxProperties &box, const QRectF &bounds,
                                      double sizePt, bool wraps, bool hasTextOrigin,
                                      const QPointF &textOriginOffset,
                                      const QList<OriginalLine> &originals,
                                      const Advance &advance)
{
    Result result;
    const bool anchorable = params.anchor && !originals.isEmpty()
        && !originals.first().charX.isEmpty()
        && !text.contains(QChar(kSoftBreak))
        && params.list == TextBoxProperties::ListStyle::None;
    if (anchorable) {
        Params flat = params;
        flat.widthPt = 0.0;
        flat.align   = TextBoxProperties::HorizontalAlign::Left;
        QList<Line> lines = lay(text, flat, advance);
        const OriginalLine &first = originals.first();
        const QPointF written(first.charX.first(), first.baseline);
        // The box carries its text along when it is moved; the original
        // objects can only stay where they are while it is not.
        const QPointF origin = hasTextOrigin ? bounds.topLeft() + textOriginOffset : written;
        const QPointF moved = origin - written;
        const bool keep = params.keep && std::hypot(moved.x(), moved.y()) < 0.01;
        double lastY = 0.0;
        int lastParagraph = 0;
        bool fits = true;
        for (Line &line : lines) {
            const int p = line.paragraph;
            if (p < originals.size() && !originals.at(p).charX.isEmpty()) {
                const OriginalLine &o = originals.at(p);
                const QStringView part = QStringView(text).mid(line.start, line.length);
                line.original   = p;
                line.x          = o.charX.first() - written.x();
                line.y          = o.baseline - written.y();
                line.keptCount  = keep ? keptPrefix(o, part) : 0;
                line.keep       = keep && part == o.text && line.keptCount == o.text.size();
                line.fixedCount = commonPrefix(o, part);
                for (int i = 0; i < line.fixedCount; ++i)
                    line.fixedX.append(o.charX.at(i) - o.charX.first());
                line.fixedX.append(restartX(o, line.fixedCount) - o.charX.first());
                // What follows the change as it was keeps its own spacing, as
                // in justified or tracked text, moved by how much the change is wider.
                line.tailCount = commonSuffix(o, part, line.fixedCount);
                if (line.tailCount > 0) {
                    const qsizetype tailStart = o.text.size() - line.tailCount;
                    for (qsizetype i = tailStart; i < o.text.size(); ++i)
                        line.tailX.append(o.charX.at(i) - o.charX.first());
                    line.tailX.append(o.endX - o.charX.first());
                    const int at = line.start + line.fixedCount;
                    double was = 0.0, now = 0.0;
                    for (const Glyph &g : glyphsOf(o.text, line.fixedCount, int(tailStart)))
                        was += advance(at, g.codePoint) + params.charSpacingPt;
                    for (const Glyph &g : glyphsOf(text, at, line.start + line.length - line.tailCount))
                        now += advance(g.index, g.codePoint) + params.charSpacingPt;
                    line.tailShift = now - was;
                }
                lastY = line.y;
                lastParagraph = p;
            } else {
                line.x = 0.0;
                line.y = lastY + (p - lastParagraph) * params.lineStepPt;
            }
            int end = line.start + line.length;
            while (end > line.start && text.at(end - 1).isSpace()) --end;
            line.width = xAt(line, text, end, params.charSpacingPt, advance);
            // A line still the document's own objects is where it is; only a
            // changed one can stop fitting.
            const bool unchanged = line.original >= 0
                && originals.at(line.original).objectStart.value(0)
                && QStringView(text).mid(line.start, line.length) == originals.at(line.original).text;
            if (wraps && !unchanged && origin.x() + line.x + line.width
                             > bounds.right() - box.paddingPt + 0.05)
                fits = false;
        }
        if (fits) {
            result.lines         = lines;
            result.firstBaseline = origin - bounds.topLeft();
            result.anchored      = true;
            return result;
        }
    }

    Params wrapped = params;
    const double inner = textWidth(box, bounds.size(),
        firstBaseline(box, bounds.size(), sizePt, {}, hasTextOrigin, textOriginOffset));
    // A box growing with its text is never narrower than it is: text shorter
    // than its least width is aligned in that width, as the file wraps it there.
    if (wraps) wrapped.widthPt = inner;
    else       wrapped.alignWidthPt = inner;
    result.lines = lay(text, wrapped, advance);
    result.firstBaseline = firstBaseline(box, bounds.size(), sizePt, result.lines,
                                         hasTextOrigin, textOriginOffset);
    return result;
}

double TextLayout::xAt(const Line &line, const QString &text, int position,
                       double charSpacing, const Advance &advance)
{
    const int relative = qBound(0, position - line.start, line.length);
    const int tailFrom = line.tailX.isEmpty() ? line.length + 1 : line.length - line.tailCount;
    if (relative >= tailFrom) return line.tailX.at(relative - tailFrom) + line.tailShift;
    const int fixed = line.fixedX.isEmpty() ? 0 : line.fixedCount;
    if (!line.fixedX.isEmpty() && relative <= fixed) return line.fixedX.at(relative);
    const int to = relative;
    double x = line.fixedX.isEmpty() ? 0.0 : line.fixedX.last();
    int glyphs = 0;
    for (const Glyph &g : glyphsOf(text, line.start + fixed, line.start + to)) {
        x += advance(g.index, g.codePoint);
        if (g.codePoint == U' ') x += line.spaceExtra;
        ++glyphs;
    }
    if (glyphs > 0) {
        const bool atEnd = to >= line.length;
        x += charSpacing * (atEnd ? glyphs - 1 : glyphs);
    }
    return x;
}

QString TextLayout::withSoftBreaks(const QString &text, const QList<Line> &lines)
{
    QString out = text;
    for (int i = lines.size() - 1; i >= 0; --i) {
        const Line &line = lines.at(i);
        if (!line.first && line.start > 0 && out.at(line.start - 1) != QChar(kSoftBreak))
            out.insert(line.start, QChar(kSoftBreak));
    }
    return out;
}

QString TextLayout::withoutSoftBreaks(QString text)
{
    return text.remove(QChar(kSoftBreak));
}
