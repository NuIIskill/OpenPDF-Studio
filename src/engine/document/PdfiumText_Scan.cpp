#include "engine/document/PdfiumText.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumFonts.hpp"
#include "engine/document/PdfiumScanInk.hpp"

#include "fpdf_edit.h"

#include <QTransform>

#include <algorithm>
#include <cmath>

namespace PdfiumText {

namespace {

// What of a mark tells the face a letter is set in: its colour and an underline change no letter's shape.
TextLayout::Mark faceOf(TextLayout::Mark mark)
{
    mark.color = QColor();
    mark.underline.reset();
    return mark;
}

}

QColor Typeset::tintAt(int position, int source) const
{
    // The colour the user gave a letter of the scan, which its ink then takes; none where it keeps its own.
    const QColor marked = m_charMarks.value(position).color;
    const bool own = !m_edit.colorChanged
                  && (!marked.isValid() || marked == m_edit.scanMarks.value(source).color || marked == m_styles.first().fill);
    return own ? QColor() : m_styles.at(styleAt(position)).fill;
}

bool Typeset::chosenColour(int style) const
{
    return m_edit.colorChanged || m_styles.at(style).fill != m_styles.first().fill;
}

QPointF Typeset::scanShift() const
{
    // How far the box carries a scan's lines from where the scan has them: nothing while it stands in its place.
    const QList<TextLayout::OriginalLine> &originals = m_edit.originalLines;
    if (originals.isEmpty() || originals.first().charX.isEmpty()) return {};
    return m_edit.pdfBounds.topLeft() + m_layout.firstBaseline
         - QPointF(originals.first().charX.first(), originals.first().baseline);
}

QTransform Typeset::turnOnScan() const
{
    // A box lying askew with its scan turns about its middle; moved, the scan's own places still turn about where that was.
    const QPointF c = m_edit.pdfBounds.center() - scanShift();
    return QTransform().translate(c.x(), c.y()).rotate(m_edit.box.rotationDeg).translate(-c.x(), -c.y());
}

QList<Typeset::ScanWord> Typeset::scanRuns(int n) const
{
    // The runs of letters of a line that follow each other as the scan has
    // them and go one way as a whole: a letter keeps the look the scan opened
    // with, as a mark the edit gave it shows only when it is set again.
    constexpr double kSamePlace = 0.05;
    const TextLayout::Line &line = m_layout.lines.at(n);
    const QList<TextLayout::OriginalLine> &originals = m_edit.originalLines;
    const double spacing = m_edit.box.characterSpacingPt;
    const TextLayout::Advance advance = advanceFunction();
    QList<int> offsets;
    for (int j = 0, at = 0; j < originals.size(); at += int(originals.at(j).text.size()) + 1, ++j) offsets.append(at);
    const QPointF first(originals.first().charX.first(), originals.first().baseline);
    const QPointF shift = scanShift();
    QList<ScanWord> runs;
    // Lines taken to another page have their pixels on the page they came from: they are set in the matched face.
    if (m_edit.sourcePage >= 0 && m_edit.sourcePage != m_edit.page) return runs;
    for (int i = 0; i < line.length; ++i) {
        const int source = line.from.at(i);
        if (source < 0 || m_text.at(line.start + i).isSpace()) continue;
        const int j = int(std::upper_bound(offsets.cbegin(), offsets.cend(), source) - offsets.cbegin()) - 1;
        const TextLayout::OriginalLine &o = originals.at(j);
        const int c = source - offsets.at(j);
        if (o.charX.size() != o.text.size() || c >= o.text.size() || o.text.at(c).isSpace()
                || faceOf(m_charMarks.value(line.start + i)) != faceOf(m_edit.scanMarks.value(source)))
            continue;
        const QPointF by = shift + QPointF(line.x + TextLayout::xAt(line, m_text, line.start + i, spacing, advance), line.y)
                         - QPointF(o.charX.at(c) - first.x(), o.baseline - first.y());
        const QColor tint = tintAt(line.start + i, source);
        // Set larger or smaller, the letters of a run stand apart by as much more or less.
        const bool follows = !runs.isEmpty() && runs.last().to == i && runs.last().line == j
                          && runs.last().at + (i - runs.last().from) == c && runs.last().tint == tint;
        const double grown = follows ? (m_scanScale - 1.0) * (o.charX.at(c) - o.charX.at(runs.last().at)) : 0.0;
        if (follows && std::hypot(by.x() - grown - runs.last().by.x(), by.y() - runs.last().by.y()) <= kSamePlace)
            runs.last().to = i + 1;
        else
            runs.append({ i, i + 1, j, c, by, tint });
    }
    return runs;
}

void Typeset::liftFromScan()
{
    // For a picture of the text alone: what would stay the scan's own pixels goes as pieces of its ink too.
    m_lifted = true;
    if (!m_scanKept.isEmpty()) keepScanInk();
}

void Typeset::keepScanInk()
{
    // A scan's letters the edit leaves alone keep its own pixels, whatever
    // was read for them: nothing the user did not type is set in the matched
    // face. Those at the start and the end of a line that stand where the
    // scan has them stay as they are; the others go where the layout puts
    // them, on their line or another, cut from their word where it changed.
    constexpr double kSamePlace = 0.05;
    const QList<TextLayout::OriginalLine> &originals = m_edit.originalLines;
    m_scanKept = QList<std::pair<int, int>>(m_layout.lines.size(), { 0, 0 });
    m_scanMoved = QList<QList<Moved>>(m_layout.lines.size());
    m_scanParts = QList<Part>(m_layout.lines.size());
    for (int n = 0; n < m_layout.lines.size(); ++n) {
        const TextLayout::Line &line = m_layout.lines.at(n);
        if (line.from.size() != line.length || !line.prefix.isEmpty()) continue;
        const QList<ScanWord> runs = scanRuns(n);
        // Ink that takes another colour or size, shows through or lies on a filled box is laid anew where it stands.
        const bool lifted = m_lifted || m_edit.box.opacity < 0.999 || m_edit.box.backgroundEnabled
                         || std::abs(m_scanScale - 1.0) > 1e-6;
        const auto stays = [&](const ScanWord &r) {
            return !lifted && !r.tint.isValid() && r.line == line.original && std::hypot(r.by.x(), r.by.y()) <= kSamePlace;
        };
        int head = 0, tail = 0, first = 0, last = int(runs.size());
        if (line.original >= 0) {
            const QString &o = originals.at(line.original).text;
            const int size = int(o.size());
            // Kept at the start: the scan's line from its first letter on, letter by letter and space by space.
            int reached = 0;
            for (int i = 0; i < line.length && i < size;) {
                if (m_text.at(line.start + i).isSpace()) {
                    if (!o.at(i).isSpace()) break;
                    ++i;
                    if (first > 0) reached = i;
                    continue;
                }
                if (first >= runs.size() || runs.at(first).from != i || runs.at(first).at != i || !stays(runs.at(first))) break;
                reached = i = runs.at(first).to;
                ++first;
            }
            head = first > 0 ? reached : 0;
            // Kept at the end: the scan's line up to its last letter.
            reached = 0;
            for (int k = 0; k < line.length - head && k < size;) {
                const int i = line.length - 1 - k;
                if (m_text.at(line.start + i).isSpace()) {
                    if (!o.at(size - 1 - k).isSpace()) break;
                    ++k;
                    if (last < runs.size()) reached = k;
                    continue;
                }
                const ScanWord *run = last > first ? &runs.at(last - 1) : nullptr;
                if (!run || run->to != i + 1 || run->at + (run->to - run->from) != size - k || !stays(*run)) break;
                reached = k = line.length - run->from;
                --last;
            }
            tail = last < runs.size() ? reached : 0;
            Part &part = m_scanParts[n];
            part.headCut = head > 0 && head < size && !o.at(head - 1).isSpace() && !o.at(head).isSpace();
            part.tailCut = tail > 0 && tail < size && !o.at(size - tail).isSpace() && !o.at(size - tail - 1).isSpace();
            // What is kept by whole words, which is what the page's text keeps: a word cut is written whole again.
            part.headWords = head;
            while (part.headCut && part.headWords > 0 && !o.at(part.headWords - 1).isSpace()) --part.headWords;
            part.tailWords = tail;
            while (part.tailCut && part.tailWords > 0 && !o.at(size - part.tailWords).isSpace()) --part.tailWords;
        }
        m_scanKept[n] = { head, tail };
        for (int r = first; r < last; ++r) {
            const ScanWord &run = runs.at(r);
            const QString &o = originals.at(run.line).text;
            const int end = run.at + (run.to - run.from);
            m_scanMoved[n].append({ run.from, run.to, run.line, run.at, run.by, true,
                                    run.at > 0 && !o.at(run.at - 1).isSpace(), end < o.size() && !o.at(end).isSpace() });
            m_scanMoved[n].last().tint = run.tint;
        }
        copyFigures(n);
    }
}

namespace {

// Figures, capitals and small letters: a typed one is told from the others of its kind.
const QString kKinds[3] = { QStringLiteral("0123456789"), QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜ"),
                            QStringLiteral("abcdefghijklmnopqrstuvwxyzäöüß") };

int kindOf(QChar c)
{
    for (int k = 0; k < 3; ++k)
        if (kKinds[k].contains(c)) return k;
    return -1;
}

}

void Typeset::copyFigures(int n)
{
    // A figure or letter typed among letters set apart is the scan's own ink of that character again, from
    // where the scan shows it set apart nearest, so it looks as the scan's whatever face that is: of the
    // edit's own lines first, else borrowed from elsewhere on the page.
    const TextLayout::Line &line = m_layout.lines.at(n);
    const QList<TextLayout::OriginalLine> &originals = m_edit.originalLines;
    const double spacing = m_edit.box.characterSpacingPt;
    const bool spaced = spacing >= m_size * 0.25;
    const auto alone = [spaced](const QString &s, qsizetype from, qsizetype to, qsizetype k) {
        return spaced || ((k == from || s.at(k - 1).isSpace()) && (k + 1 >= to || s.at(k + 1).isSpace()));
    };
    const TextLayout::Advance advance = advanceFunction();
    const QPointF first(originals.first().charX.first(), originals.first().baseline);
    // Where a point of the edit's lines lies on the page, and the pictures an earlier edit laid over the scan there.
    const QPointF shift = scanShift();
    const QList<QRectF> laid = PdfiumScanInk::pictures(m_page);
    bool added = false;
    if (m_edit.sourcePage >= 0 && m_edit.sourcePage != m_edit.page) return;
    for (int i = 0; i < line.length; ++i) {
        const qsizetype at = line.start + i;
        const QChar typed = m_text.at(at);
        if (line.from.at(i) >= 0 || kindOf(typed) < 0 || !alone(m_text, line.start, line.start + line.length, at)) continue;
        const QPointF to(line.x + TextLayout::xAt(line, m_text, int(at), spacing, advance), line.y);
        int source = -1, sourceAt = 0;
        double least = 0.0;
        QPointF by;
        for (int j = 0, offset = 0; j < originals.size(); offset += int(originals.at(j).text.size()) + 1, ++j) {
            const TextLayout::OriginalLine &o = originals.at(j);
            if (o.charX.size() != o.text.size()) continue;
            for (qsizetype c = 0; c < o.text.size(); ++c) {
                if (o.text.at(c) != typed || !alone(o.text, 0, o.text.size(), c)
                        || faceOf(m_charMarks.value(at)) != faceOf(m_edit.scanMarks.value(offset + c)))
                    continue;
                const QPointF from(o.charX.at(c) - first.x(), o.baseline - first.y());
                // One of its own line before one of another, the scan's own print before a picture laid on it.
                const QPointF inside = first + from + QPointF(advance(int(at), typed.unicode()) / 2.0, -m_size * 0.3);
                const bool foreign = std::any_of(laid.cbegin(), laid.cend(), [&](const QRectF &r) { return r.contains(inside); });
                const double distance = std::abs(to.x() - from.x()) + std::abs(to.y() - from.y()) * 8.0 + (foreign ? 1e7 : 0.0);
                if (source >= 0 && distance >= least) continue;
                source = j;
                sourceAt = int(c);
                least = distance;
                by = shift + to - from;
            }
        }
        // Borrowed from the page where the edit's lines show none of the scan's own print: for a box that lies
        // level, in the scan's own look. What an earlier edit laid on the line is the last to be taken.
        const bool plain = faceOf(m_charMarks.value(at)) == faceOf(m_edit.scanMarks.value(0));
        const bool borrows = plain && qFuzzyIsNull(m_edit.box.rotationDeg);
        if (source >= 0 && least >= 1e7 && borrows) source = -1;
        if (source < 0 && !borrows) continue;
        if (source >= 0) m_scanMoved[n].append({ i, i + 1, source, sourceAt, by, true, false, false, 0.0, 0.0, true, false });
        else m_scanMoved[n].append({ i, i + 1, 0, 0, to, true, false, false, 0.0, 0.0, true, true });
        // What is typed in a colour of its own is the scan's ink in that colour.
        if (chosenColour(styleAt(int(at)))) m_scanMoved[n].last().tint = m_styles.at(styleAt(int(at))).fill;
        added = true;
    }
    if (added)
        std::stable_sort(m_scanMoved[n].begin(), m_scanMoved[n].end(), [](const Moved &a, const Moved &b) { return a.from < b.from; });
}

QList<Typeset::Glyphs> Typeset::shapes() const
{
    QList<Glyphs> out;
    if (m_styles.isEmpty()) return out;
    const int style = TextLayout::dominantStyle(m_charStyles, int(m_styles.size()));
    for (const QString &kind : kKinds) {
        Glyphs glyphs;
        for (const QChar c : kind) {
            QPainterPath glyph;
            FPDF_FONT font = fontOf(style, c.unicode());
            if (!font || !PdfiumFonts::glyphOutline(font, c.unicode(), m_size, &glyph)) continue;
            glyphs.chars.append(c);
            glyphs.paths.append(glyph);
        }
        out.append(glyphs);
    }
    return out;
}

QList<Typeset::ScanCut> Typeset::cutsOnScan() const
{
    // Where a line is cut inside a word, on the page: between two letters as
    // the layout has them. First where what it keeps at its start and its end
    // ends, then at each end of what it moves.
    QList<ScanCut> out;
    const QList<TextLayout::OriginalLine> &originals = m_edit.originalLines;
    if (originals.isEmpty() || originals.first().charX.isEmpty()) return out;
    // Where the scan has its lines, whatever way the box has gone since.
    const QTransform turn = turnOnScan();
    const QPointF origin;
    const TextLayout::Advance advance = advanceFunction();
    // In the middle of the white the letter before leaves, by what the face gives that letter; with how
    // many letters the cut's word has, and how many of them stand before it.
    // On which side of it what is kept or moved lies: before it, or after.
    const auto cut = [&](int line, qsizetype k, int side) {
        const TextLayout::OriginalLine &o = originals.at(line);
        qsizetype a = k, b = k;
        while (a > 0 && !o.text.at(a - 1).isSpace()) --a;
        while (b < o.text.size() && !o.text.at(b).isSpace()) ++b;
        const double step = o.charX.at(k) - o.charX.at(k - 1);
        const double before = advance(0, o.text.at(k - 1).unicode());
        const double back = -qBound(0.0, step - before, step) / 2.0;
        // As far as the middles of the letters on either side, beyond which a white is another letter's.
        const double after = (k + 1 < o.charX.size() ? o.charX.at(k + 1) : o.endX) - o.charX.at(k);
        return ScanCut { turn.map(origin + QPointF(o.charX.at(k) + back, o.baseline)), m_size, int(b - a), int(k - a), back, side,
                         qMax(0.0, step + back - qMin(before, step) / 2.0), qMax(0.0, after / 2.0 - back) };
    };
    for (int n = 0; n < m_layout.lines.size() && n < m_scanParts.size(); ++n) {
        const int own = m_layout.lines.at(n).original;
        if (m_scanParts.at(n).headCut) out.append(cut(own, m_scanKept.at(n).first, -1));
        if (m_scanParts.at(n).tailCut) out.append(cut(own, originals.at(own).text.size() - m_scanKept.at(n).second, 1));
        for (const Moved &m : m_scanMoved.at(n)) {
            if (m.cutLeft) out.append(cut(m.source, m.at, 1));
            if (m.cutRight) out.append(cut(m.source, m.at + (m.to - m.from), -1));
        }
    }
    return out;
}

void Typeset::setCuts(const QList<ScanCut> &cuts, const QList<double> &shiftsPt, const QList<bool> &outward)
{
    // Each cut by how far before its letter the scan's white lies; where none
    // was found, where the layout has it. A cut set outward to the next white,
    // as two letters touch where it was to be, gives up the letters it passes:
    // they are set anew with what was typed, and no letter is cut in two.
    const QList<TextLayout::OriginalLine> &originals = m_edit.originalLines;
    int at = 0;
    const auto next = [&](bool *out) {
        const double shift = shiftsPt.value(at, 0.0), back = cuts.value(at).back;
        *out = outward.value(at);
        ++at;
        return back + (std::isnan(shift) ? 0.0 : shift);
    };
    // Whether the middle of a letter of a line lies before a cut set by a shift from another letter of it.
    const auto before = [&](int line, qsizetype c, qsizetype from, double shift) {
        const TextLayout::OriginalLine &o = originals.at(line);
        const double end = c + 1 < o.charX.size() ? o.charX.at(c + 1) : o.endX;
        return (o.charX.at(c) + end) / 2.0 < o.charX.at(from) + shift;
    };
    for (int n = 0; n < m_scanParts.size(); ++n) {
        Part &part = m_scanParts[n];
        const int own = m_layout.lines.at(n).original;
        bool out = false;
        if (part.headCut) {
            const int head = m_scanKept.at(n).first;
            part.headShift = next(&out);
            int kept = head;
            while (out && kept > part.headWords && !before(own, kept - 1, head, part.headShift)) --kept;
            part.headShift += originals.at(own).charX.at(head) - originals.at(own).charX.at(kept);
            m_scanKept[n].first = kept;
        }
        if (part.tailCut) {
            const int size = int(originals.at(own).text.size()), from = size - m_scanKept.at(n).second;
            part.tailShift = next(&out);
            int kept = from;
            while (out && kept < size - part.tailWords && before(own, kept, from, part.tailShift)) ++kept;
            part.tailShift += originals.at(own).charX.at(from) - (kept < size ? originals.at(own).charX.at(kept) : originals.at(own).endX);
            m_scanKept[n].second = size - kept;
        }
        // The cuts of each piece as they were asked for, then pieces that touch are joined, then the cuts are made.
        QList<Moved> &moved = m_scanMoved[n];
        QList<std::pair<bool, bool>> outs;
        for (Moved &m : moved) {
            bool left = false, right = false;
            if (m.cutLeft) m.shiftLeft = next(&left);
            if (m.cutRight) m.shiftRight = next(&right);
            outs.append({ left, right });
        }
        joinTouching(moved, outs);
        for (int k = 0; k < moved.size(); ++k) {
            Moved &m = moved[k];
            if (m.cutLeft) {
                const int from = m.at;
                while (outs.at(k).first && m.from < m.to && before(m.source, m.at, from, m.shiftLeft)) { ++m.from; ++m.at; }
                if (m.from < m.to) m.shiftLeft += originals.at(m.source).charX.at(from) - originals.at(m.source).charX.at(m.at);
            }
            if (m.cutRight) {
                const int end = m.at + (m.to - m.from);
                int last = end;
                while (outs.at(k).second && last > m.at && !before(m.source, last - 1, end, m.shiftRight)) --last;
                const TextLayout::OriginalLine &o = originals.at(m.source);
                const auto x = [&](int c) { return c < o.charX.size() ? o.charX.at(c) : o.endX; };
                m.shiftRight += x(end) - x(last);
                m.to = m.from + (last - m.at);
            }
            if (m.from >= m.to) m.pixels = false;
        }
    }
}

void Typeset::joinTouching(QList<Moved> &moved, QList<std::pair<bool, bool>> &outward)
{
    // Two letters of the scan that go different ways but touch, with no white to cut them in, go together
    // the way the first of them goes: so neither is cut in two nor set anew, as where a spacing parts all letters.
    for (int k = 0; k + 1 < moved.size();) {
        Moved &a = moved[k];
        const Moved &b = moved.at(k + 1);
        const bool beside = a.pixels && b.pixels && !a.copied && !b.copied && a.cutRight && b.cutLeft && a.to == b.from
                         && a.source == b.source && a.at + (a.to - a.from) == b.at && a.tint == b.tint;
        if (!beside || !(outward.at(k).second || outward.at(k + 1).first)) { ++k; continue; }
        a.to = b.to;
        a.cutRight = b.cutRight;
        a.shiftRight = b.shiftRight;
        outward[k].second = outward.at(k + 1).second;
        moved.removeAt(k + 1);
        outward.removeAt(k + 1);
    }
}

QList<Typeset::ScanMove> Typeset::movedOnScan() const
{
    // Each moved word where the scan has it, along its baseline on the page,
    // and how far it goes; words the scan has side by side that go alike are a run.
    QList<ScanMove> out;
    const QList<TextLayout::OriginalLine> &originals = m_edit.originalLines;
    if (originals.isEmpty() || originals.first().charX.isEmpty()) return out;
    const QRectF &bounds = m_edit.pdfBounds;
    const QPointF c = bounds.center();
    const QTransform turn = QTransform().translate(c.x(), c.y()).rotate(m_edit.box.rotationDeg)
                                        .translate(-c.x(), -c.y());
    // Where the scan has its lines, whatever way the box has gone since; where they go, the box as it stands tells.
    const QTransform home = turnOnScan();
    const QPointF origin;
    for (int n = 0; n < m_layout.lines.size() && n < m_scanMoved.size(); ++n) {
        const QList<Moved> &moved = m_scanMoved.at(n);
        const auto alike = [&](int a, int b) {
            if (a < 0 || b >= moved.size()) return false;
            const Moved &u = moved.at(a), &v = moved.at(b);
            return !u.copied && !v.copied && !u.cutRight && !v.cutLeft && u.to + 1 == v.from && u.source == v.source
                && u.at + (u.to - u.from) + 1 == v.at && std::hypot(u.by.x() - v.by.x(), u.by.y() - v.by.y()) <= 0.05;
        };
        for (int k = 0; k < moved.size(); ++k) {
            const Moved &m = moved.at(k);
            if (m.borrowed) {
                // Where the typed character's own ink would lie: there the borrowed one goes.
                const QChar typed = m_text.at(m_layout.lines.at(n).start + m.from);
                ScanMove move;
                move.size = m_size;
                move.kind = kindOf(typed);
                move.borrowed = true;
                move.tint = m.tint;
                move.opacity = m_edit.box.opacity;
                move.scale = m_scanScale;
                const QPointF target = bounds.topLeft() + m_layout.firstBaseline + m.by;
                move.word = QLineF(target, target + QPointF(1.0, 0.0));
                QPainterPath glyph;
                FPDF_FONT font = fontOf(TextLayout::dominantStyle(m_charStyles, int(m_styles.size())), typed.unicode());
                if (font && PdfiumFonts::glyphOutline(font, typed.unicode(), m_size, &glyph)) {
                    move.figure = int(typed.unicode());
                    move.onto = glyph.boundingRect().translated(target);
                }
                out.append(move);
                continue;
            }
            const TextLayout::OriginalLine &o = originals.at(m.source);
            const auto at = [&](qsizetype i) { return origin + QPointF(i < o.charX.size() ? o.charX.at(i) : o.endX, o.baseline); };
            const QPointF was = home.map(at(m.at));
            // Letters of a changed word start and end at the cuts that part them from it.
            const QPointF start = m.cutLeft ? home.map(at(m.at) + QPointF(m.shiftLeft, 0.0)) : was;
            const QPointF end = home.map(at(m.at + (m.to - m.from)) + QPointF(m.cutRight ? m.shiftRight : 0.0, 0.0));
            out.append({ QLineF(start, end), m_size, turn.map(at(m.at) + m.by) - was, !alike(k - 1, k), !alike(k, k + 1),
                         m.cutLeft, m.cutRight,
                         m.copied ? int(m_text.at(m_layout.lines.at(n).start + m.from).unicode()) : -1,
                         m.copied ? kindOf(m_text.at(m_layout.lines.at(n).start + m.from)) : 0, false, {} });
            out.last().tint = m.tint;
            out.last().opacity = m_edit.box.opacity;
            out.last().scale = m_scanScale;
            out.last().from = was;
        }
    }
    return out;
}

QList<bool> Typeset::changedOnScan() const
{
    // The lines of the scan the edit changes: those not all of whose characters the text still holds.
    const QList<TextLayout::OriginalLine> &originals = m_edit.originalLines;
    QList<bool> changed;
    QList<int> offsets;
    qsizetype all = 0;
    for (const TextLayout::OriginalLine &o : originals) {
        offsets.append(int(all));
        all += o.text.size() + 1;
    }
    QList<bool> held(all, false);
    for (const TextLayout::Line &line : m_layout.lines)
        for (const int source : line.from)
            if (source >= 0 && source < all) held[source] = true;
    for (int j = 0; j < originals.size(); ++j) {
        bool whole = true;
        for (qsizetype i = 0; i < originals.at(j).text.size() && whole; ++i) whole = held.at(offsets.at(j) + i);
        changed.append(!whole);
    }
    return changed;
}

QString Typeset::sampleKey() const
{
    // The lines the edit changes, by which its sample is chosen.
    QString key;
    const QList<bool> changed = changedOnScan();
    for (int j = 0; j < changed.size(); ++j)
        if (changed.at(j)) key += QString::number(j) + u',';
    return key;
}

bool Typeset::printedOnScan() const
{
    // Text set on a scan shows as the scan printed it, in the colour it is given.
    return m_edit.scanned;
}

bool Typeset::outlineOf(int style, FPDF_FONT font, const QList<uint> &codePoints, const QList<double> &x,
                        double baseline, Outline *outline, bool onScan) const
{
    // The glyphs of a run where it is set on the shown page, the box's turn included.
    const Style &s = m_styles.at(style);
    QPainterPath run;
    // Outlines that cross, as of an accent on its letter, are ink all the same.
    run.setFillRule(Qt::WindingFill);
    for (int i = 0; i < codePoints.size(); ++i) {
        if (QChar::isSpace(codePoints.at(i))) continue;
        QPainterPath glyph;
        // Where the scan has its words they are as large as the scan has them.
        if (!PdfiumFonts::glyphOutline(font, codePoints.at(i), onScan ? s.size / m_scanScale : s.size, &glyph)) return false;
        run.addPath(QTransform(s.a, 0.0, -s.c, 1.0, x.at(i), baseline - s.rise).map(glyph));
    }
    const QPointF c = m_edit.pdfBounds.center();
    outline->path = (onScan ? turnOnScan() : QTransform().translate(c.x(), c.y()).rotate(m_edit.box.rotationDeg).translate(-c.x(), -c.y())).map(run);
    outline->strokePt = s.mode == FPDF_TEXTRENDERMODE_FILL_STROKE ? s.strokeWidth : 0.0;
    outline->fill = s.fill;
    return true;
}

QList<Typeset::Outline> Typeset::sample() const
{
    // A few of the scan's own words, the longest, set in the edit's face where
    // the scan has them: held against their pixels they tell how the scan
    // printed. Those of the lines the edit changes come first and alone, as
    // a line beside them may be printed heavier or lighter.
    constexpr int kWords = 5;
    QList<Outline> made;
    const QList<TextLayout::OriginalLine> &originals = m_edit.originalLines;
    if (!m_layout.anchored || m_styles.isEmpty() || originals.isEmpty() || originals.first().charX.isEmpty()) return made;
    struct Word { int line; qsizetype from, to; bool changed; };
    QList<Word> words;
    const QList<bool> changed = changedOnScan();
    qsizetype offset = 0;
    for (int j = 0; j < originals.size(); offset += originals.at(j).text.size() + 1, ++j) {
        const TextLayout::OriginalLine &o = originals.at(j);
        if (o.charX.size() != o.text.size()) continue;
        for (qsizetype c = 0; c < o.text.size();) {
            if (o.text.at(c).isSpace()) { ++c; continue; }
            qsizetype e = c;
            int letters = 0;
            bool plain = true;
            for (; e < o.text.size() && !o.text.at(e).isSpace(); ++e) {
                letters += o.text.at(e).isLetterOrNumber();
                plain = plain && m_edit.scanMarks.value(offset + e).isEmpty();
            }
            if (plain && letters >= 1) words.append({ j, c, e, changed.value(j) });
            c = e;
        }
    }
    // Words of three letters and more tell best; a line of single figures has only those.
    const bool anyChanged = std::any_of(words.cbegin(), words.cend(), [](const Word &w) { return w.changed; });
    const auto tells = [&](const Word &w) { return w.to - w.from >= 3 && (w.changed || !anyChanged); };
    if (std::any_of(words.cbegin(), words.cend(), tells))
        words.removeIf([](const Word &w) { return w.to - w.from < 3; });
    if (std::any_of(words.cbegin(), words.cend(), [](const Word &w) { return w.changed; }))
        words.removeIf([](const Word &w) { return !w.changed; });
    std::stable_sort(words.begin(), words.end(), [](const Word &a, const Word &b) { return a.to - a.from > b.to - b.from; });
    const int style = TextLayout::dominantStyle(m_charStyles, int(m_styles.size()));
    const QPointF origin;
    for (const Word &word : words.mid(0, kWords)) {
        const TextLayout::OriginalLine &o = originals.at(word.line);
        QList<uint> codes;
        QList<double> x;
        FPDF_FONT font = nullptr;
        bool one = true;
        for (qsizetype c = word.from; c < word.to && one; ++c) {
            const char32_t code = o.text.at(c).unicode();
            FPDF_FONT with = fontOf(style, code);
            one = with && (!font || with == font);
            font = with;
            codes.append(code);
            x.append(origin.x() + o.charX.at(c));
        }
        Outline outline;
        if (one && font && outlineOf(style, font, codes, x, origin.y() + o.baseline, &outline, true)) made.append(outline);
    }
    return made;
}

bool Typeset::insertOnScan(FPDF_PAGE page, int style, FPDF_FONT font, const QList<uint> &codes,
                           const QList<double> &x, double baseline, const PdfiumPageSpace &space, bool hidden) const
{
    // Text printed on a scan shows as pixels in the scan's look, drawn from
    // its outlines; the text itself is set for reading and does not show.
    Outline outline;
    const bool drawn = !hidden && printedOnScan() && outlineOf(style, font, codes, x, baseline, &outline);
    outline.chosen = chosenColour(style);
    outline.opacity = m_edit.box.opacity;
    if (drawn) m_printed.append(outline);
    if (!hidden && !drawn) return false;
    // A scan without a text layer stays one: what shows as its pixels adds no text to the page.
    if (!m_layered) return true;
    // Text that does not show is set word by word, as an OCR layer holds it, so it is read like one.
    for (qsizetype from = 0; from < codes.size();) {
        qsizetype to = from;
        while (to < codes.size() && !QChar::isSpace(codes.at(to))) ++to;
        while (to < codes.size() && QChar::isSpace(codes.at(to))) ++to;
        insertRun(page, style, font, codes.mid(from, to - from), x.mid(from, to - from), baseline, space, true);
        from = to;
    }
    return true;
}

void Typeset::setAgain(int move)
{
    // A moved word whose pixels cannot be had is set in the matched face like the changed ones.
    for (QList<Moved> &moved : m_scanMoved)
        for (Moved &m : moved)
            if (move-- == 0) m.pixels = false;
}

QList<Typeset::ScanInk> Typeset::keptOnScan(bool words) const
{
    // The stretches of each line along its baseline, on the page, whose pixels
    // stay: from beyond the line's start to halfway into the gap after the
    // last word kept there, and from halfway into the gap before the first
    // word kept at its end. Kept characters stand where the scan has them.
    QList<ScanInk> out;
    if (m_scanKept.isEmpty()) return out;
    const QRectF &bounds = m_edit.pdfBounds;
    const QPointF base = m_layout.firstBaseline;
    const QPointF c = bounds.center();
    const QTransform turn = QTransform().translate(c.x(), c.y()).rotate(m_edit.box.rotationDeg)
                                        .translate(-c.x(), -c.y());
    const double reach = m_size * 3.0;
    for (int n = 0; n < m_layout.lines.size() && n < m_scanKept.size(); ++n) {
        // By whole words for the page's text, which holds a word in one object; letter by letter for its pixels.
        Part part = m_scanParts.value(n);
        if (words) part.headCut = part.tailCut = false;
        const int head = words ? part.headWords : m_scanKept.at(n).first, tail = words ? part.tailWords : m_scanKept.at(n).second;
        if (head == 0 && tail == 0) continue;
        const TextLayout::Line &line = m_layout.lines.at(n);
        const TextLayout::OriginalLine &o = m_edit.originalLines.at(line.original);
        // Kept words stand where the scan has them, wherever the rest of their line goes.
        const TextLayout::OriginalLine &first = m_edit.originalLines.first();
        const double baseline = bounds.top() + base.y() + o.baseline - first.baseline;
        const double lineX = bounds.left() + base.x() + o.charX.first() - first.charX.first();
        const auto x = [&](qsizetype k) { return lineX + (k < o.charX.size() ? o.charX.at(k) : o.endX) - o.charX.first(); };
        const double start = lineX - reach, end = x(o.text.size()) + reach;
        const auto mapped = [&](double from, double to) {
            return QLineF(turn.map(QPointF(from, baseline)), turn.map(QPointF(to, baseline)));
        };
        if (head >= line.length && line.length == o.text.size()) {
            out.append({ mapped(start, end), m_size, {}, 0 });
            continue;
        }
        // A space starts where the word before it ends; the word kept next to
        // where the cover starts or ends goes along, so the stretch ends on its area.
        const auto wordAround = [&](qsizetype k) {
            qsizetype a = k, b = k;
            while (a > 0 && !o.text.at(a - 1).isSpace()) --a;
            while (b < o.text.size() && !o.text.at(b).isSpace()) ++b;
            return mapped(x(a), x(b));
        };
        // A cut inside a word lies in the white between two of its letters, where the scan was found to have it.
        if (head > 0 && part.headCut) {
            out.append({ mapped(start, x(head) + part.headShift), m_size, {}, 0 });
        } else if (head > 0) {
            const qsizetype last = o.text.at(head - 1).isSpace() ? head - 2 : head - 1;
            const double cut = o.text.at(head - 1).isSpace() ? (x(head - 1) + x(head)) / 2.0 : (x(head) + x(head + 1)) / 2.0;
            out.append({ mapped(start, cut), m_size, wordAround(qMax<qsizetype>(0, last)), -1 });
        }
        const qsizetype from = o.text.size() - tail;
        if (tail > 0 && part.tailCut)
            out.append({ mapped(x(from) + part.tailShift, end), m_size, {}, 0 });
        else if (tail > 0) {
            // What is kept at the end may begin with the space before its first word, as behind a gap
            // that took up the change: the stretch is that word's, from halfway into the gap before it.
            qsizetype first = from;
            while (first < o.text.size() && o.text.at(first).isSpace()) ++first;
            if (first < o.text.size())
                out.append({ mapped(first > 0 ? (x(first - 1) + x(first)) / 2.0 : start, end), m_size, wordAround(first), 1 });
        }
    }
    return out;
}
}

#endif
