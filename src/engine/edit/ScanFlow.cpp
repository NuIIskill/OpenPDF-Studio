#include "engine/edit/ScanFlow.hpp"

#include <QPointF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ScanFlow {

namespace {

using TextLayout::Advance;
using TextLayout::Line;
using TextLayout::OriginalLine;
using TextLayout::Params;

// A word fits behind another where this share of a space still parts them.
constexpr double kFits = 0.7;
// A justified line closes each of its gaps by up to this share of a space.
constexpr double kCloses = 0.25;

// A word of the text: one of the scan's own, whole, or typed.
struct Word {
    int a { 0 }, b { 0 };
    int line { 0 };
    int source { -1 };
    int ca { 0 }, cb { 0 };
    double width { 0.0 };
    int spaces { 0 };
};

// A line of the text as typed, up to its break, and the line of the scan it is.
struct TextLine {
    int start { 0 }, end { 0 };
    int first { 0 }, count { 0 };
    int own { -1 };
    bool intact { false };
    bool pristine { false };
    bool softAfter { false };
};

// A line as shown, and the line of the scan whose place it takes.
struct Shown {
    Line line;
    int slot { -1 };
};

// Where a line of its own stands, the share of it lying left of there, and the width it has room for.
struct Stand {
    double at { 0.0 };
    double share { 0.0 };
    double room { 0.0 };
};

class Flow
{
public:
    Flow(const QString &text, const Params &params, double sizePt, const QList<OriginalLine> &originals,
         const Advance &advance, double low, double high);
    QList<Line> lines() const;

private:
    bool usable(int j) const;
    double xOf(int j, int c) const;
    double leftOf(int j) const { return m_originals.at(j).charX.first(); }
    double typedWidth(int a, int b) const;
    double within(const Word &w, int to) const;
    int replacedBy(const Word &w, int i) const;
    void readOriginals();
    void readText();
    void readWord(Word &w) const;
    double gapBefore(int w, bool even) const;
    Line legacy(int p) const;
    Line placed(int from, int to, double left, const QList<double> &starts) const;
    Stand standOf(int own, double left) const;
    Shown blank(int p, double left) const;
    void rows(int p, double left, QList<Shown> &out) const;
    void settle(int from, int to, int end, double left, bool justified, QList<double> &starts) const;
    void lay(int first, int last, QList<Shown> &out) const;
    void reflow(int firstLine, int lastLine, const QList<int> &places, int taken, bool justified,
                QList<Shown> &out) const;

    const QString &m_text;
    const Params &m_params;
    const double m_size;
    const QList<OriginalLine> &m_originals;
    const Advance &m_advance;
    const double m_low, m_high;
    QPointF m_written;
    double m_space { 0.0 }, m_right { 0.0 }, m_step { 0.0 };
    bool m_flowing { false };
    QList<int> m_src, m_lineAt, m_charAt, m_offset;
    QList<bool> m_soft;
    QList<Word> m_words;
    QList<TextLine> m_lines;
};

Flow::Flow(const QString &text, const Params &params, double sizePt, const QList<OriginalLine> &originals,
           const Advance &advance, double low, double high)
    : m_text(text), m_params(params), m_size(sizePt), m_originals(originals), m_advance(advance), m_low(low), m_high(high)
{
    m_written = QPointF(originals.first().charX.first(), originals.first().baseline);
    m_space = advance(0, U' ') + params.charSpacingPt;
    readOriginals();
    readText();
}

bool Flow::usable(int j) const
{
    return j >= 0 && j < m_originals.size() && !m_originals.at(j).text.isEmpty()
        && m_originals.at(j).charX.size() == m_originals.at(j).text.size();
}

double Flow::xOf(int j, int c) const
{
    const OriginalLine &o = m_originals.at(j);
    // Each letter stands as much further on as the box sets its letters wider than the scan has them.
    return (c < o.charX.size() ? o.charX.at(c) : o.endX) + m_params.scanWiderPt * qBound(0, c, qMax(0, int(o.text.size()) - 1));
}

double Flow::typedWidth(int a, int b) const
{
    double width = 0.0;
    for (int i = a; i < b; ++i) {
        char32_t cp = m_text.at(i).unicode();
        if (m_text.at(i).isHighSurrogate() && i + 1 < b && m_text.at(i + 1).isLowSurrogate())
            cp = QChar::surrogateToUcs4(m_text.at(i), m_text.at(i + 1));
        else if (m_text.at(i).isLowSurrogate() && i > a)
            continue;
        width += m_advance(i, cp) + m_params.charSpacingPt;
    }
    return width;
}

double Flow::within(const Word &w, int to) const
{
    // How far into a changed word a letter stands: a letter of the scan takes the
    // step the scan has to its next, so the scan's letters keep their places among
    // each other and go as its pixels; a typed one takes what the face gives it.
    double x = 0.0;
    for (int i = w.a; i < to; ++i) {
        const int s = m_src.at(i);
        if (s >= 0 && m_charAt.at(s) >= 0 && usable(m_lineAt.at(s))) {
            // The line's last letter ends where its ink does: what follows it stands the letter spacing off, as behind any other.
            const int j = m_lineAt.at(s), c = m_charAt.at(s);
            x += xOf(j, c + 1) - xOf(j, c) + (c + 1 == m_originals.at(j).text.size() && i + 1 < w.b ? m_params.charSpacingPt : 0.0);
            continue;
        }
        const double typed = typedWidth(i, i + 1);
        // A letter typed for one of the scan's, one for one, takes that letter's room and what it is wider
        // or narrower: so what follows it stays where the scan has it.
        if (const int r = replacedBy(w, i); r >= 0) {
            const int j = m_lineAt.at(r), c = m_charAt.at(r);
            const QString &t = m_originals.at(j).text;
            const double room = xOf(j, c + 1) - xOf(j, c) + (c + 1 == t.size() && i + 1 < w.b ? m_params.charSpacingPt : 0.0);
            x += qMax(typed * 0.5, room + typed - (m_advance(i, t.at(c).unicode()) + m_params.charSpacingPt));
            continue;
        }
        x += typed;
    }
    return x;
}

int Flow::replacedBy(const Word &w, int i) const
{
    // The scan's letter a typed one stands for: where as many were typed between two of the scan's
    // letters, or between one and the word's end, as the scan has letters there.
    if (m_text.at(i).isSurrogate()) return -1;
    const auto scanAt = [&](int k) {
        const int s = m_src.at(k);
        return s >= 0 && m_charAt.at(s) >= 0 && usable(m_lineAt.at(s)) ? s : -1;
    };
    int l = i - 1, r = i + 1;
    while (l >= w.a && scanAt(l) < 0) --l;
    while (r < w.b && scanAt(r) < 0) ++r;
    const int p = l >= w.a ? scanAt(l) : -1, q = r < w.b ? scanAt(r) : -1;
    if (p < 0 && q < 0) return -1;
    const int j = m_lineAt.at(p >= 0 ? p : q);
    const QString &t = m_originals.at(j).text;
    // The letters replaced, from the first to behind the last, within their line.
    const int from = p >= 0 ? m_charAt.at(p) + 1 : m_charAt.at(q) - (r - w.a);
    const int to = q >= 0 ? m_charAt.at(q) : m_charAt.at(p) + (w.b - l);
    if ((p >= 0 && q >= 0 && (m_lineAt.at(q) != j || to - from != r - l - 1)) || from < 0 || to > t.size() || to <= from) return -1;
    if ((p < 0 && from > 0 && !t.at(from - 1).isSpace()) || (q < 0 && to < t.size() && !t.at(to).isSpace())) return -1;
    for (int c = from; c < to; ++c)
        if (t.at(c).isSpace()) return -1;
    return m_offset.at(j) + from + (i - l - 1);
}

void Flow::readOriginals()
{
    const int n = int(m_originals.size());
    for (int j = 0; j < n; ++j) {
        const OriginalLine &o = m_originals.at(j);
        m_offset.append(int(m_lineAt.size()));
        for (int c = 0; c < o.text.size(); ++c) {
            m_lineAt.append(j);
            m_charAt.append(c);
        }
        if (j + 1 < n) {
            m_lineAt.append(j);
            m_charAt.append(-1);
        }
        if (usable(j)) m_right = qMax(m_right, o.endX);
    }
    // The step from one line to the next is the closest two of them stand.
    for (int j = 1; j < n; ++j) {
        if (!usable(j) || !usable(j - 1)) continue;
        const double step = m_originals.at(j).baseline - m_originals.at(j - 1).baseline;
        if (step >= m_size * 0.8 && (m_step <= 0.0 || step < m_step)) m_step = step;
    }
    // A single line tells none; what its box says holds only where it is a step of text of this size.
    if (m_step <= 0.0) {
        m_step = m_params.lineStepPt;
        if (m_step < m_size * 1.05 || m_step > m_size * 1.5) m_step = m_size * 1.2;
    }
    // A line broke where its paragraph flowed on when the first word of the
    // next had no room behind it, the next starts no further in, or below a
    // word of it as a list's line does, and stands a usual step below. A
    // short line of a few words, as of an address, is a line of its own.
    constexpr int kWords = 4;
    constexpr double kWide = 18.0, kLeading = 1.55;
    for (int j = 0; j + 1 < n; ++j) {
        bool soft = false;
        if (usable(j) && usable(j + 1)) {
            const OriginalLine &a = m_originals.at(j), &b = m_originals.at(j + 1);
            int e = 0;
            while (e < b.text.size() && !b.text.at(e).isSpace()) ++e;
            bool under = leftOf(j + 1) <= leftOf(j) + m_size * 0.5;
            for (int c = 1, words = 0; c < a.text.size() && words < 2 && !under; ++c)
                if (!a.text.at(c).isSpace() && a.text.at(c - 1).isSpace()) {
                    under = std::abs(a.charX.at(c) - leftOf(j + 1)) <= m_size * 0.15;
                    ++words;
                }
            int words = 0;
            for (int c = 0; c < a.text.size(); ++c) words += !a.text.at(c).isSpace() && (c == 0 || a.text.at(c - 1).isSpace());
            // Lines set far apart, as the items of a list, flow on only where they end flush at the right edge.
            const bool close = b.baseline - a.baseline <= m_size * kLeading || m_right - a.endX <= m_size * 0.1 + 0.5;
            soft = a.endX + kFits * m_space + xOf(j + 1, e) - leftOf(j + 1) > m_right && under && close
                && b.baseline - a.baseline <= m_step * 1.3 && words >= kWords && a.endX - leftOf(j) >= m_size * kWide;
        }
        m_soft.append(soft);
        m_flowing = m_flowing || soft;
    }
}

void Flow::readWord(Word &w) const
{
    const int s0 = m_src.at(w.a), length = w.b - w.a;
    bool whole = s0 >= 0 && m_charAt.at(s0) >= 0 && usable(m_lineAt.at(s0)) && s0 + length <= m_lineAt.size()
              && m_lineAt.at(s0 + length - 1) == m_lineAt.at(s0) && m_charAt.at(s0 + length - 1) >= 0;
    for (int k = 1; k < length && whole; ++k) whole = m_src.at(w.a + k) == s0 + k;
    if (whole) {
        const int j = m_lineAt.at(s0), c0 = m_charAt.at(s0), c1 = c0 + length;
        const QString &t = m_originals.at(j).text;
        if ((c0 == 0 || t.at(c0 - 1).isSpace()) && (c1 == t.size() || t.at(c1).isSpace())) {
            w.source = j;
            w.ca = c0;
            w.cb = c1;
            w.width = xOf(j, c1) - xOf(j, c0);
            return;
        }
    }
    w.width = within(w, w.b);
}

void Flow::readText()
{
    m_src = TextLayout::sourcesOf(m_text, TextLayout::originalText(m_originals));
    const int n = int(m_originals.size());
    for (int start = 0; start <= m_text.size();) {
        int end = int(m_text.indexOf(u'\n', start));
        if (end < 0) end = int(m_text.size());
        TextLine tl;
        tl.start = start;
        tl.end = end;
        tl.first = int(m_words.size());
        for (int i = start, spaces = 0; i < end;) {
            if (m_text.at(i).isSpace()) { ++spaces; ++i; continue; }
            Word w;
            w.a = i;
            while (i < end && !m_text.at(i).isSpace()) ++i;
            w.b = i;
            w.line = int(m_lines.size());
            w.spaces = spaces;
            spaces = 0;
            readWord(w);
            m_words.append(w);
        }
        tl.count = int(m_words.size()) - tl.first;
        // The line of the scan most of it comes from; it is that line still where nothing of another is in it.
        QList<int> tally(n, 0);
        int sources = 0;
        for (int i = start; i < end; ++i)
            if (const int s = m_src.at(i); s >= 0 && m_charAt.at(s) >= 0) ++tally[m_lineAt.at(s)];
        for (int j = 0; j < n; ++j) {
            if (!tally.at(j)) continue;
            ++sources;
            if (tl.own < 0 || tally.at(j) > tally.at(tl.own)) tl.own = j;
        }
        tl.intact = sources == 1 && usable(tl.own);
        // A line whose letters the box sets wider or closer than the scan has them is not as the scan has it.
        tl.pristine = tl.intact && QStringView(m_text).mid(start, end - start) == m_originals.at(tl.own).text
                   && qFuzzyIsNull(m_params.scanWiderPt);
        if (end < m_text.size())
            if (const int s = m_src.at(end); s >= 0 && m_charAt.at(s) < 0) tl.softAfter = m_soft.value(m_lineAt.at(s));
        m_lines.append(tl);
        start = end + 1;
    }
}

double Flow::gapBefore(int w, bool even) const
{
    // Two words the scan has side by side keep the gap it has between them;
    // others stand a space apart, as do two parted by a break the paragraph flowed over.
    const Word &u = m_words.at(w - 1), &v = m_words.at(w);
    if (u.line != v.line) return m_space;
    if (!even && u.source >= 0 && v.source == u.source && v.ca > u.cb) {
        const QString &t = m_originals.at(u.source).text;
        bool beside = true;
        for (int c = u.cb; c < v.ca && beside; ++c) beside = t.at(c).isSpace();
        if (beside) return xOf(u.source, v.ca) - xOf(u.source, u.cb);
    }
    return m_space * qMax(1, v.spaces);
}

Line Flow::legacy(int p) const
{
    // The line where the scan has it, as far as it is still that line.
    const TextLine &tl = m_lines.at(p);
    const OriginalLine &o = m_originals.at(tl.own);
    Line l;
    l.start = tl.start;
    l.length = tl.end - tl.start;
    l.paragraph = p;
    l.first = true;
    l.original = tl.own;
    TextLayout::anchorLine(l, m_text, o, m_written, m_params, m_size, m_advance, false);
    int end = tl.end;
    while (end > tl.start && m_text.at(end - 1).isSpace()) --end;
    l.width = TextLayout::xAt(l, m_text, end, m_params.charSpacingPt, m_advance);
    // A line of its own set flush right grows to its left, a centred one to both sides.
    if (!tl.softAfter && (p == 0 || !m_lines.at(p - 1).softAfter)) {
        // By less than half a point it has not grown: a letter typed over another is as wide as the face has it, not as the scan.
        double grown = l.width - (o.endX - o.charX.first());
        if (std::abs(grown) < 0.5) grown = 0.0;
        if (m_params.align == TextBoxProperties::HorizontalAlign::Right) l.x -= grown;
        else if (m_params.align == TextBoxProperties::HorizontalAlign::Center) l.x -= grown / 2.0;
    }
    l.from = QList<int>(l.length, -1);
    const int off = m_offset.at(tl.own), size = int(o.text.size());
    for (int i = 0; i < l.fixedCount && i < l.length; ++i) l.from[i] = off + i;
    for (int k = 0; k < l.tailCount && k < l.length; ++k) l.from[l.length - l.tailCount + k] = off + size - l.tailCount + k;
    return l;
}

Line Flow::placed(int from, int to, double left, const QList<double> &starts) const
{
    // Some words on one line, each where it was put; what stands between two is spread over their gap.
    const Word &head = m_words.at(from), &last = m_words.at(to - 1);
    const TextLine &hl = m_lines.at(head.line), &ll = m_lines.at(last.line);
    Line l;
    l.start = from == hl.first ? hl.start : head.a;
    const int end = to == ll.first + ll.count ? ll.end : last.b;
    l.length = end - l.start;
    l.paragraph = head.line;
    l.first = true;
    l.x = left - m_written.x();
    l.placedX = QList<double>(l.length + 1, 0.0);
    l.from = QList<int>(l.length, -1);
    double at = 0.0;
    int done = l.start;
    for (int w = from; w < to; ++w) {
        const Word &word = m_words.at(w);
        const double x = starts.at(w - from) - left;
        for (int i = done; i < word.a; ++i) l.placedX[i - l.start] = at + (x - at) * (i - done) / (word.a - done);
        for (int i = word.a; i < word.b; ++i) {
            l.placedX[i - l.start] = x + (word.source >= 0 ? xOf(word.source, word.ca + i - word.a) - xOf(word.source, word.ca)
                                                           : within(word, i));
            l.from[i - l.start] = m_src.at(i);
        }
        at = x + word.width;
        done = word.b;
    }
    for (int i = done; i <= end; ++i) l.placedX[i - l.start] = at + m_space * (i - done);
    l.width = at;
    return l;
}

Stand Flow::standOf(int own, double left) const
{
    // A line set flush right ends where the scan's does and one centred keeps its middle; each has the room up to its box's edges.
    using Align = TextBoxProperties::HorizontalAlign;
    if (!usable(own) || (m_params.align != Align::Right && m_params.align != Align::Center)) {
        const double at = usable(own) ? leftOf(own) : left;
        return { at, 0.0, m_high - at };
    }
    const double end = m_originals.at(own).endX;
    if (m_params.align == Align::Right) return { end, 1.0, end - m_low };
    const double mid = (leftOf(own) + end) / 2.0;
    return { mid, 0.5, 2.0 * qMin(mid - m_low, m_high - mid) };
}

Shown Flow::blank(int p, double left) const
{
    const TextLine &tl = m_lines.at(p);
    Line l;
    l.start = tl.start;
    l.length = tl.end - tl.start;
    l.paragraph = p;
    l.first = true;
    l.x = left - m_written.x();
    l.from = QList<int>(l.length, -1);
    for (int i = 0; i <= l.length; ++i) l.placedX.append(m_space * i);
    return { l, tl.own };
}

void Flow::rows(int p, double left, QList<Shown> &out) const
{
    // A line of its own: its words one after the other, on a further line where its room ends.
    const TextLine &tl = m_lines.at(p);
    const Stand stand = standOf(tl.own, left);
    if (tl.count == 0) {
        out.append(blank(p, stand.at));
        return;
    }
    for (int i = tl.first, end = tl.first + tl.count; i < end;) {
        QList<double> starts;
        double x = i == tl.first ? m_words.at(i).spaces * m_space : 0.0;
        int j = i;
        for (; j < end; ++j) {
            const double gap = j > i ? gapBefore(j, false) : 0.0;
            if (j > i && x + gap + m_words.at(j).width > stand.room + 0.05) break;
            x += gap;
            starts.append(x);
            x += m_words.at(j).width;
        }
        // A word longer than the room runs on past it: moved left it would stand on what is beside the line.
        const double from = stand.at - stand.share * x;
        for (double &start : starts) start += from;
        out.append({ placed(i, j, from, starts), i == tl.first ? tl.own : -1 });
        i = j;
    }
}

void Flow::settle(int from, int to, int end, double left, bool justified, QList<double> &starts) const
{
    // A line that is one of the scan's, whole, keeps the places the scan gave
    // its words; another of a justified paragraph is spread to its right edge.
    const Word &head = m_words.at(from), &last = m_words.at(to - 1);
    bool whole = head.source >= 0 && head.ca == 0 && last.source == head.source
              && last.cb == m_originals.at(head.source).text.size();
    for (int w = from + 1; w < to && whole; ++w) {
        const Word &u = m_words.at(w - 1), &v = m_words.at(w);
        whole = v.source == u.source && v.ca > u.cb;
        const QString &t = m_originals.at(head.source).text;
        for (int c = u.cb; c < v.ca && whole; ++c) whole = t.at(c).isSpace();
    }
    if (whole) {
        for (int w = from; w < to; ++w) starts[w - from] = left + xOf(head.source, m_words.at(w).ca) - leftOf(head.source);
        return;
    }
    if (!justified || to - from < 2) return;
    const double each = (m_right - (starts.last() + last.width)) / (to - from - 1);
    // The last line of the paragraph is closed up where it is too long, and spread no wider.
    if (to >= end && each >= 0.0) return;
    if (each <= -m_space * 0.3 || each >= m_space * 6.0) return;
    for (int w = from; w < to; ++w) starts[w - from] += each * (w - from);
}

void Flow::reflow(int firstLine, int lastLine, const QList<int> &places, int taken, bool justified,
                  QList<Shown> &out) const
{
    // The words from a line on fill line after line up to the paragraph's right edge.
    const int from = m_lines.at(firstLine).first;
    const int to = m_lines.at(lastLine).first + m_lines.at(lastLine).count;
    double lastLeft = out.isEmpty() ? m_written.x() : m_written.x() + out.last().line.x;
    if (from == to) {
        out.append(blank(firstLine, usable(m_lines.at(firstLine).own) ? leftOf(m_lines.at(firstLine).own) : lastLeft));
        return;
    }
    for (int i = from, k = taken; i < to; ++k) {
        const Word &head = m_words.at(i);
        const bool startsLine = i == m_lines.at(head.line).first;
        // From where a line starts as the scan has it and all after it are the scan's still, they stay.
        if (startsLine) {
            bool rest = true;
            for (int p = head.line; p <= lastLine && rest; ++p)
                rest = m_lines.at(p).pristine && (p == head.line || m_lines.at(p).own == m_lines.at(p - 1).own + 1);
            if (rest) {
                for (int p = head.line; p <= lastLine; ++p) out.append({ legacy(p), m_lines.at(p).own });
                return;
            }
        }
        const int slot = k < places.size() ? places.at(k) : -1;
        const double left = head.source >= 0 && head.ca == 0 ? leftOf(head.source)
                          : usable(slot) ? leftOf(slot)
                          : !places.isEmpty() && usable(places.last()) ? leftOf(places.last()) : lastLeft;
        QList<double> starts;
        double x = left + (startsLine ? head.spaces * m_space : 0.0);
        starts.append(x);
        x += head.width;
        int j = i + 1;
        for (; j < to; ++j) {
            // A justified line may close each of its gaps a little to take a word; another keeps the gaps it has.
            const double gap = gapBefore(j, justified);
            const double over = x + gap + m_words.at(j).width - m_right;
            if (over > m_space * (justified ? kCloses * (j - i) : 0.3)) break;
            x += gap;
            starts.append(x);
            x += m_words.at(j).width;
        }
        settle(i, j, to, left, justified, starts);
        out.append({ placed(i, j, left, starts), slot });
        lastLeft = left;
        i = j;
    }
}

void Flow::lay(int first, int last, QList<Shown> &out) const
{
    // The lines of the scan these lines of the text stand on, from the top.
    int low = std::numeric_limits<int>::max(), high = -1;
    for (int p = first; p <= last; ++p) {
        const TextLine &tl = m_lines.at(p);
        if (tl.own >= 0) { low = qMin(low, tl.own); high = qMax(high, tl.own); }
        for (int w = tl.first; w < tl.first + tl.count; ++w)
            if (const int j = m_words.at(w).source; j >= 0) { low = qMin(low, j); high = qMax(high, j); }
    }
    QList<int> places;
    for (int j = low; j <= high; ++j) places.append(j);
    // They flow where the scan's paragraph did: over its own breaks, or as one line holding several of its lines.
    bool flows = last > first;
    if (!flows && high > low) {
        flows = true;
        for (int j = low; j < high; ++j) flows = flows && m_soft.value(j);
    }
    // A line that is no line of the scan breaks at the right edge its paragraph has.
    flows = flows || (m_flowing && !m_lines.at(first).intact);
    // Justified where the lines the paragraph flowed over all end at its right edge.
    // A single such line tells by its gaps, spread wider than a space.
    int full = 0;
    bool flush = true;
    QList<double> gaps;
    for (const int j : std::as_const(places))
        if (m_soft.value(j)) {
            ++full;
            flush = flush && m_right - m_originals.at(j).endX <= m_size * 0.1 + 0.5;
            const OriginalLine &o = m_originals.at(j);
            for (int c = 1; c < o.text.size(); ++c)
                if (!o.text.at(c).isSpace() && o.text.at(c - 1).isSpace()) gaps.append(o.charX.at(c) - o.charX.at(c - 1));
        }
    std::sort(gaps.begin(), gaps.end());
    const bool justified = flush && (full >= 2 || (full == 1 && !gaps.isEmpty() && gaps.at(gaps.size() / 2) >= m_space * 1.25));
    const int begin = int(out.size());
    for (int p = first; p <= last; ++p) {
        const TextLine &tl = m_lines.at(p);
        if (!flows) {
            // A line of its own stays one while it has room; a line as the scan has it stays in any case.
            // A centred one that changed by too little to move has its room where it lies, not about its middle.
            const Line own = tl.intact ? legacy(p) : Line();
            const double left = m_written.x() + own.x;
            const bool roomy = m_params.align == TextBoxProperties::HorizontalAlign::Center
                ? left >= m_low - 0.05 && left + own.width <= m_high + 0.05 : own.width <= standOf(tl.own, 0.0).room + 0.05;
            if (tl.intact && (tl.pristine || roomy))
                out.append({ own, tl.own });
            else
                rows(p, out.isEmpty() ? m_written.x() : m_written.x() + out.last().line.x, out);
            continue;
        }
        bool again = !tl.intact || (p > first && tl.own != m_lines.at(p - 1).own + 1);
        Line line;
        if (!again) {
            // A line too long for the paragraph passes words on; one with room for the next line's first takes it.
            line = legacy(p);
            const double end = m_written.x() + line.x + line.width;
            again = end > m_right + m_space * 0.3;
            if (!again && p < last && m_lines.at(p + 1).count > 0)
                again = end + kFits * m_space + m_words.at(m_lines.at(p + 1).first).width <= m_right;
            // A changed line of a justified paragraph is spread to its right edge again.
            if (!again && justified && p < last && !tl.pristine) again = std::abs(end - m_right) > 0.5;
        }
        if (again) {
            reflow(p, last, places, int(out.size()) - begin, justified, out);
            return;
        }
        out.append({ line, tl.own });
    }
}

QList<Line> Flow::lines() const
{
    QList<Shown> shown;
    for (int p = 0; p < m_lines.size();) {
        int q = p;
        while (q + 1 < m_lines.size() && m_lines.at(q).softAfter) ++q;
        lay(p, q, shown);
        p = q + 1;
    }
    // Each line as far below the one before as the scan's own lines stood apart there, a new one by the usual step.
    QList<Line> out;
    double y = 0.0;
    for (int v = 0; v < shown.size(); ++v) {
        const int slot = shown.at(v).slot;
        if (v > 0)
            y += usable(slot) && usable(slot - 1) ? m_originals.at(slot).baseline - m_originals.at(slot - 1).baseline : m_step;
        Line line = shown.at(v).line;
        line.y = y;
        out.append(line);
    }
    return out;
}

}

QList<TextLayout::Line> lines(const QString &text, const TextLayout::Params &params, double sizePt,
                              const QList<TextLayout::OriginalLine> &originals,
                              const TextLayout::Advance &advance, double low, double high)
{
    return Flow(text, params, sizePt, originals, advance, low, high).lines();
}

}
