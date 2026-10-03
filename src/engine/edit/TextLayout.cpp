#include "engine/edit/TextLayout.hpp"

#include <QtGlobal>

#include <limits>

namespace {

constexpr double kTolerance = 0.05;

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

double widthOf(const QString &text, int from, int to,
               const std::function<double(char32_t)> &advance, double charSpacing)
{
    while (to > from && text.at(to - 1).isSpace()) --to;
    const QList<Glyph> glyphs = glyphsOf(text, from, to);
    double width = 0.0;
    for (const Glyph &g : glyphs) width += advance(g.codePoint);
    if (glyphs.size() > 1) width += charSpacing * (glyphs.size() - 1);
    return width;
}

QList<Range> wrap(const QString &text, int from, int to, double maxWidth,
                  const std::function<double(char32_t)> &advance, double charSpacing)
{
    const QList<Glyph> glyphs = glyphsOf(text, from, to);
    if (glyphs.isEmpty()) return { { from, 0 } };

    QList<double> bis { 0.0 };
    double breite = 0.0;
    for (int i = 0; i < glyphs.size(); ++i) {
        breite += advance(glyphs.at(i).codePoint);
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
                                        const std::function<double(char32_t)> &advance)
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
            ? 0.0 : widthOf(prefix, 0, prefix.size(), advance, params.charSpacingPt)
                    + advance(U' ') + params.charSpacingPt;
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
        ++paragraph;
        from = end + 1;
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
    if (hasTextOrigin && !customInset) return textOriginOffset;

    const double contentHeight = sizePt + (lines.isEmpty() ? 0.0 : lines.constLast().y);
    const double innerHeight = qMax(0.0, bounds.height() - 2 * box.paddingPt);
    double offset = 0.0;
    if (box.verticalAlign == TextBoxProperties::VerticalAlign::Center)
        offset = qMax(0.0, (innerHeight - contentHeight) / 2.0);
    else if (box.verticalAlign == TextBoxProperties::VerticalAlign::Bottom)
        offset = qMax(0.0, innerHeight - contentHeight);
    return QPointF(box.paddingPt + box.indentLevel * 18.0,
                   box.paddingPt + offset + sizePt * 0.8);
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

int TextLayout::keptPrefix(const OriginalLine &original, QStringView line)
{
    const int common = commonPrefix(original, line);
    if (common >= original.text.size()) return int(original.text.size());
    int kept = 0;
    for (int i = 0; i <= common && i < original.objectStart.size(); ++i)
        if (original.objectStart.at(i)) kept = i;
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
                                      const std::function<double(char32_t)> &advance)
{
    Result result;
    const bool anchorable = !originals.isEmpty() && !originals.first().charX.isEmpty()
        && !text.contains(QChar(kSoftBreak))
        && params.list == TextBoxProperties::ListStyle::None;
    if (anchorable) {
        Params flat = params;
        flat.widthPt = 0.0;
        flat.align   = TextBoxProperties::HorizontalAlign::Left;
        QList<Line> lines = lay(text, flat, advance);
        const OriginalLine &first = originals.first();
        const QPointF origin(first.charX.first(), first.baseline);
        double lastY = 0.0;
        int lastParagraph = 0;
        bool fits = true;
        for (Line &line : lines) {
            const int p = line.paragraph;
            if (p < originals.size() && !originals.at(p).charX.isEmpty()) {
                const OriginalLine &o = originals.at(p);
                const QStringView part = QStringView(text).mid(line.start, line.length);
                line.original   = p;
                line.keep       = part == o.text;
                line.x          = o.charX.first() - origin.x();
                line.y          = o.baseline - origin.y();
                line.keptCount  = keptPrefix(o, part);
                line.fixedCount = commonPrefix(o, part);
                for (int i = 0; i < line.fixedCount; ++i)
                    line.fixedX.append(o.charX.at(i) - o.charX.first());
                line.fixedX.append(restartX(o, line.fixedCount) - o.charX.first());
                lastY = line.y;
                lastParagraph = p;
            } else {
                line.x = 0.0;
                line.y = lastY + (p - lastParagraph) * params.lineStepPt;
            }
            int end = line.start + line.length;
            while (end > line.start && text.at(end - 1).isSpace()) --end;
            line.width = xAt(line, text, end, params.charSpacingPt, advance);
            if (wraps && origin.x() + line.x + line.width
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
    if (wraps)
        wrapped.widthPt = textWidth(box, bounds.size(),
            firstBaseline(box, bounds.size(), sizePt, {}, hasTextOrigin, textOriginOffset));
    result.lines = lay(text, wrapped, advance);
    result.firstBaseline = firstBaseline(box, bounds.size(), sizePt, result.lines,
                                         hasTextOrigin, textOriginOffset);
    return result;
}

double TextLayout::xAt(const Line &line, const QString &text, int position,
                       double charSpacing, const std::function<double(char32_t)> &advance)
{
    const int relative = qBound(0, position - line.start, line.length);
    const int fixed = line.fixedX.isEmpty() ? 0 : line.fixedCount;
    if (!line.fixedX.isEmpty() && relative <= fixed) return line.fixedX.at(relative);
    double x = line.fixedX.isEmpty() ? 0.0 : line.fixedX.last();
    int glyphs = 0;
    for (const char32_t cp : QStringView(text).mid(line.start + fixed, relative - fixed).toUcs4()) {
        x += advance(cp);
        ++glyphs;
    }
    if (glyphs > 0) {
        const bool atEnd = relative >= line.length;
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
