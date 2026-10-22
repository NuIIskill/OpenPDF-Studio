#include "ui/edit/EditController.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/document/DocumentSource.hpp"
#include "engine/render/PdfRenderer.hpp"
#include "ui/edit/TextBoxFrame.hpp"

#include <QFont>
#include <QFontMetricsF>

#include <algorithm>
#include <cmath>

namespace {

struct Spot { int line { -1 }; int first { 0 }; int end { 0 }; bool joined { false }; };

// Where a framed line sits in the lines of the paragraph or box it came from.
Spot spotOf(const QList<TextLayout::OriginalLine> &lines, const TextLayout::OriginalLine &framed)
{
    const double from = framed.charX.first(), to = framed.endX;
    for (int l = 0; l < lines.size(); ++l) {
        const TextLayout::OriginalLine &line = lines.at(l);
        if (std::abs(line.baseline - framed.baseline) > 0.5) continue;
        for (int i = 0; i < line.charX.size(); ++i) {
            // Characters of a ligature share their place: each is tried.
            if (std::abs(line.charX.at(i) - from) > 0.05) continue;
            const int end = i + int(framed.text.size());
            if (line.text.mid(i, framed.text.size()) != framed.text) continue;
            const double at = end < line.charX.size() ? line.charX.at(end) : line.endX;
            if (std::abs(at - to) > 0.05) continue;
            return { l, i, end, false };
        }
    }
    // A gap framed between two pieces: the second goes onto the first.
    if (!framed.text.trimmed().isEmpty()) return {};
    for (int l = 0; l + 1 < lines.size(); ++l)
        if (std::abs(lines.at(l).endX - from) < 0.05 && !lines.at(l + 1).charX.isEmpty()
                && std::abs(lines.at(l + 1).charX.first() - to) < 0.05
                && std::abs(lines.at(l + 1).baseline - framed.baseline) < 1.0)
            return { l, int(lines.at(l).text.size()), int(lines.at(l).text.size()), true };
    return {};
}


}

std::optional<EditSession::Edit> EditController::mergedFrameEdit(const QString &text) const
{
    // The paragraph or box the frame was cut from, with the framed text
    // replaced where it was: the characters before it stay where they are,
    // the rest of its line follows the new text.
    const QList<TextLayout::OriginalLine> &framed = activeEditOriginalLines;
    if (text.contains(QChar(TextLayout::kSoftBreak)) || framed.isEmpty()) return std::nullopt;
    // Lines whose break was deleted along with them are empty.
    QStringList parts = text.split(u'\n');
    QList<QList<double>> xs = m_frame->paragraphCharXPt();
    if (parts.size() > framed.size() || (xs.size() != parts.size() && !text.isEmpty()))
        return std::nullopt;
    while (parts.size() < framed.size()) parts.append(QString());
    while (xs.size() < parts.size()) xs.append(QList<double>{ 0.0 });

    QList<TextLayout::OriginalLine> lines = m_unit.originalLines;
    QList<QList<TextLayout::Source>> sources = m_unitSources;
    if (sources.size() != lines.size()) {
        sources.clear();
        for (const TextLayout::OriginalLine &line : std::as_const(lines))
            sources.append(QList<TextLayout::Source>(line.text.size()));
    }
    for (int k = int(framed.size()) - 1; k >= 0; --k) {
        const TextLayout::OriginalLine &f = framed.at(k);
        if (f.charX.isEmpty()) return std::nullopt;
        const Spot spot = spotOf(lines, f);
        if (spot.line < 0) return std::nullopt;
        if (spot.joined) {
            TextLayout::OriginalLine &a = lines[spot.line];
            const TextLayout::OriginalLine b = lines.at(spot.line + 1);
            a.text        += b.text;
            a.charX       += b.charX;
            a.objectStart += QList<bool>(b.text.size(), false);
            a.endX         = b.endX;
            a.rect         = a.rect.united(b.rect);
            sources[spot.line] += sources.at(spot.line + 1);
            lines.removeAt(spot.line + 1);
            sources.removeAt(spot.line + 1);
        }
        TextLayout::OriginalLine &line = lines[spot.line];
        QList<TextLayout::Source> &own = sources[spot.line];
        const QString part = parts.at(k);
        // An emptied editor still reports the places its last text had.
        const QList<double> at = part.isEmpty() ? QList<double>{ 0.0 } : xs.value(k, { 0.0 });
        if (at.size() != part.size() + 1 || own.size() != line.text.size()) return std::nullopt;
        const double from = f.charX.first(), oldWidth = f.endX - from, newWidth = at.last();
        const bool tail = spot.end < line.text.size();
        const TextLayout::Source style = own.value(spot.first, own.value(qMax(0, spot.first - 1)));

        // The rest of the line follows the new text, as in any editor: closer
        // where it is shorter, further where it is longer.
        QString spaces;
        QList<double> spaceX;
        double shift = 0.0;
        if (tail) {
            shift = newWidth - oldWidth;
            // A gap read back as one wide space with the word space before
            // it keeps a word space when taken out whole between two words.
            QList<double> words;
            for (int i = 0; i + 1 < line.text.size() && i + 1 < line.charX.size(); ++i)
                if (line.text.at(i) == u' ' && (i < spot.first || i >= spot.end))
                    words.append(line.charX.at(i + 1) - line.charX.at(i));
            std::sort(words.begin(), words.end());
            double word = words.isEmpty() ? 0.0 : words.at(words.size() / 2);
            if (words.isEmpty()) {
                // No other space on the line: the font's own.
                QFont font(currentEditorFontFamily.isEmpty() ? QStringLiteral("Helvetica") : currentEditorFontFamily);
                font.setPixelSize(100);
                font.setBold(currentEditorBold);
                font.setItalic(currentEditorItalic);
                word = QFontMetricsF(font).horizontalAdvance(u' ') * currentEditorFontSizePt / 100.0;
            }
            const bool between = spot.first > 0 && !line.text.at(spot.first - 1).isSpace()
                               && !line.text.at(spot.end).isSpace()
                               && from <= line.charX.at(spot.first) + 0.5;
            if (part.isEmpty() && f.text.trimmed().isEmpty() && between && oldWidth > word * 1.6) {
                spaces = QStringLiteral(" ");
                spaceX = { from };
                shift = word - oldWidth;
            }
        }

        TextLayout::OriginalLine merged;
        merged.baseline = line.baseline;
        merged.text  = line.text.left(spot.first) + part + spaces + line.text.mid(spot.end);
        merged.charX = line.charX.mid(0, spot.first);
        for (int i = 0; i < part.size(); ++i) merged.charX.append(from + at.at(i));
        merged.charX += spaceX;
        for (int i = spot.end; i < line.charX.size(); ++i) merged.charX.append(line.charX.at(i) + shift);
        // A changed line is set anew; its original objects go.
        merged.objectStart = QList<bool>(merged.text.size(), false);
        merged.endX = tail ? line.endX + shift : from + newWidth;
        const QList<TextLayout::Source> mergedSources = own.mid(0, spot.first)
            + QList<TextLayout::Source>(part.size() + spaces.size(), style) + own.mid(spot.end);
        if (merged.text.trimmed().isEmpty()) {
            lines.removeAt(spot.line);
            sources.removeAt(spot.line);
            continue;
        }
        const double left = qMin(line.rect.left(), merged.charX.first());
        merged.rect = QRectF(left, line.rect.top(),
                             qMax(merged.endX, line.rect.right() + shift) - left, line.rect.height());
        line = merged;
        own  = mergedSources;
    }

    EditSession::Edit e = m_unit;
    QStringList texts;
    QList<TextLayout::Source> flat;
    QRectF bounds = e.pdfBounds;
    for (int l = 0; l < lines.size(); ++l) {
        texts << lines.at(l).text;
        if (l > 0) flat.append(TextLayout::Source{});
        flat += sources.at(l);
        // The layout measures a line to its pen's end, which can lie past its ink.
        const TextLayout::OriginalLine &line = lines.at(l);
        bounds = bounds.united(line.rect).united(QRectF(line.rect.left(), line.rect.top(),
                                                        line.endX - line.rect.left(), line.rect.height()));
    }
    e.newText       = texts.join(u'\n');
    e.originalLines = lines;
    // Characters without a style of their own take it from the original ones.
    bool styled = true;
    for (const QList<TextLayout::Source> &own : std::as_const(sources))
        for (const TextLayout::Source &source : own) styled &= source.isValid();
    e.sources       = styled ? flat : QList<TextLayout::Source>();
    if (!e.sourceRect.isNull()) bounds = bounds.united(e.sourceRect);
    // A scan's lines are set anew in the face its pixels match, as wide as
    // that makes them, which its text layer's boxes may not reach.
    if (m_unit.scanned) {
        for (const QRectF &r : m_unit.eraseRects) bounds = bounds.united(r);
        bounds.setLeft(qMax(0.0, bounds.left()));
        QFont font(e.fontFamily.isEmpty() ? QStringLiteral("Helvetica") : e.fontFamily);
        font.setPixelSize(100);
        font.setBold(e.bold);
        font.setItalic(e.italic);
        font.setKerning(false);
        const QFontMetricsF fm(font);
        const double left = e.hasTextOrigin ? e.pdfBounds.left() + e.textOriginOffset.x() : bounds.left();
        for (const QString &line : std::as_const(texts))
            bounds.setRight(qMax(bounds.right(), left + fm.horizontalAdvance(line) * e.fontSizePt / 100.0 + 2.0));
    }
    // A point of room on the right, but not past the page: a line pushed
    // further wraps the paragraph instead of running off it.
    double right = bounds.right() + e.box.paddingPt + 1.0;
    if (m_src->renderer()) right = qMin(right, m_src->renderer()->pageSizePts(e.page).width());
    bounds.setRight(right);
    // The blank shares the bounds, so the text counts as what replaces it.
    e.sourceRect    = bounds;
    e.pdfBounds     = bounds;
    e.box.bounds    = bounds;
    if (m_unit.scanned && m_unit.hasTextOrigin) {
        e.textOriginOffset = m_unit.pdfBounds.topLeft() + m_unit.textOriginOffset - bounds.topLeft();
    } else if (!lines.isEmpty()) {
        e.hasTextOrigin    = true;
        e.textOriginOffset = QPointF(lines.first().charX.first(), lines.first().baseline)
                           - bounds.topLeft();
    }
    return e;
}

#endif
