#include "engine/forms/FormTextLayout.hpp"

#include <QFontMetricsF>
#include <QStringList>

#include <iterator>

namespace FormTextLayout {

namespace {

// The sizes PDFium chooses an automatic font size from; a multi-line field uses the first quarter.
constexpr int kSteps[] = { 4, 6, 8, 9, 10, 12, 14, 18, 20, 25, 30, 35, 40, 45, 50, 55, 60,
                           70, 80, 90, 100, 110, 120, 130, 144 };
constexpr qreal kMeasure = 2048.0;

// Helvetica's widths per 1000 units for the printable ASCII and Latin-1 characters, as PDFium
// uses them for the standard font; others are measured with the font Qt finds for it.
constexpr short kAscii[95] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584 };
constexpr short kLatin1[96] = {
    278, 333, 556, 556, 556, 556, 260, 556, 333, 737, 370, 556, 584, 333, 737, 333,
    400, 584, 333, 333, 333, 556, 537, 278, 333, 333, 365, 556, 834, 834, 834, 611,
    667, 667, 667, 667, 667, 667, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278,
    722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611,
    556, 556, 556, 556, 556, 556, 889, 500, 556, 556, 556, 556, 278, 278, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 584, 611, 556, 556, 556, 556, 500, 556, 500 };

// Typographic characters PDFium draws from a substitute font, with the widths it gives them.
int substituteWidth(ushort c)
{
    switch (c) {
    case 0x2013: case 0x2014: case 0x2018: case 0x2019: case 0x201C: case 0x201D:
    case 0x2026: case 0x2030: case 0x2122: return 1000;
    case 0x201A: case 0x201E: case 0x2022: case 0x2039: return 1261;
    case 0x20AC: return 668;
    case 0x2020: return 522;
    case 0x0160: return 611;
    case 0x0152: case 0x0192: return 376;
    case 0x02C6: return 283;
    default: return 0;
    }
}

qreal widthAt(const QFontMetricsF &metrics, const QString &text, qreal size)
{
    qreal units = 0;
    for (const QChar ch : text) {
        const ushort c = ch.unicode();
        if (c >= 32 && c < 127)            units += kAscii[c - 32];
        else if (c >= 160 && c < 256)      units += kLatin1[c - 160];
        else if (const int w = substituteWidth(c)) units += w;
        else                               units += metrics.horizontalAdvance(ch) * 1000 / kMeasure;
    }
    return units * size / 1000;
}

}

QFont font()
{
    QFont f(QStringLiteral("Helvetica"));
    f.setStyleHint(QFont::Helvetica);
    // Unhinted advances add up the way PDFium places the glyphs.
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

qreal textWidth(const QString &text, qreal size)
{
    QFont f = font();
    f.setPixelSize(int(kMeasure));
    return widthAt(QFontMetricsF(f), text, size);
}

namespace {

// How PDFium's line breaking sees a character: a new word starts after a space or closing
// punctuation, and at an opening bracket or a currency sign; hyphens, slashes and the like join.
enum class Kind { Joining, Word, Space, Closing, Opening, Prefix };

Kind kindOf(QChar ch)
{
    switch (ch.unicode()) {
    case u' ': case 0x3000:
        return Kind::Space;
    case u'!': case u'"': case u')': case u':': case u';': case u'?': case u']': case u'}':
    case 0x2018: case 0x2019: case 0x201C: case 0x201D:
        return Kind::Closing;
    case u'(': case u'[': case u'{': case 0x201A: case 0x201E:
        return Kind::Opening;
    case u'$': case 0x20AC:
        return Kind::Prefix;
    default:
        return ch.isLetterOrNumber() ? Kind::Word : Kind::Joining;
    }
}

bool division(QChar previous, QChar current)
{
    const Kind cur = kindOf(current), prev = kindOf(previous);
    if (cur == Kind::Prefix) return prev != Kind::Prefix;
    return cur == Kind::Word && (prev == Kind::Space || prev == Kind::Closing);
}

}

QList<int> lineLengths(const QString &paragraph, qreal width, qreal size)
{
    QFont f = font();
    f.setPixelSize(int(kMeasure));
    const QFontMetricsF metrics(f);
    // PDFium's CPVT_Section::SplitLines: on overflow back to the last word start on the line,
    // else break before the character; a character alone on a line stays there.
    QList<int> lengths;
    const int n = int(paragraph.size());
    int head = 0, wordStart = 0, fullWords = 0, chars = 0, i = 0;
    qreal lineWidth = 0, backup = 0;
    bool opened = false;
    while (i < n) {
        const QChar ch = paragraph.at(i);
        const QChar previous = i > 0 ? paragraph.at(i - 1) : ch;
        const qreal w = widthAt(metrics, QString(ch), size);
        bool fullWord = false;
        if (!opened) {
            if (kindOf(ch) == Kind::Opening) opened = fullWord = true;
            else fullWord = division(previous, ch);
        } else if (kindOf(ch) != Kind::Space && kindOf(ch) != Kind::Opening) {
            opened = false;
        }
        if (fullWord) {
            if (chars > 0) ++fullWords;
            wordStart = i;
            backup = lineWidth;
        }
        ++chars;
        if (width > 0 && lineWidth + w > width) {
            if (fullWords > 0) {
                i = wordStart;
                lineWidth = backup;
            }
            if (chars == 1) ++i;
            lengths << i - head;
            head = i;
            lineWidth = 0;
            chars = fullWords = 0;
        } else {
            lineWidth += w;
            ++i;
        }
    }
    if (head < n || lengths.isEmpty()) lengths << n - head;
    return lengths;
}

int lineCount(const QString &text, qreal width, qreal size)
{
    int lines = 0;
    for (const QString &paragraph : text.split(u'\n')) lines += int(lineLengths(paragraph, width, size).size());
    return lines;
}

qreal fontSize(const QString &text, const QSizeF &box, bool multiline, qreal fixed)
{
    if (fixed > 0) return fixed;
    const qreal plateWidth  = box.width() - 2 * kInset;
    const qreal plateHeight = box.height() - 2 * kInset;
    if (plateWidth <= 0) return kSteps[0];
    QFont f = font();
    f.setPixelSize(int(kMeasure));
    const QFontMetricsF metrics(f);
    const auto bigger = [&](qreal size) {
        if (multiline) return lineCount(text, plateWidth, size) * kLineHeight * size > plateHeight + 0.0001;
        return kLineHeight * size > plateHeight + 0.0001 || widthAt(metrics, text, size) > plateWidth + 0.0001;
    };
    // The same search as PDFium's, so ties land on the same step.
    int total = int(std::size(kSteps));
    if (multiline) total /= 4;
    int left = 0, right = total - 1, mid = total / 2;
    while (left <= right) {
        if (bigger(kSteps[mid])) right = mid - 1;
        else                     left = mid + 1;
        mid = (left + right) / 2;
    }
    return kSteps[qMax(0, mid)];
}

qreal singleBaseline(qreal height, qreal size)
{
    return height / 2 + size * (kAscent - kLineHeight / 2);
}

qreal firstBaseline(qreal size)
{
    return kInset + kAscent * size;
}

}
