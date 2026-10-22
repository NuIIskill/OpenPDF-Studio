#include "ui/edit/InlineEditor.hpp"

#include "engine/edit/SourcedText.hpp"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>

namespace {

// Where a pasted character's style comes from, kept in its format so it moves with the text.
constexpr int kSourcePage = QTextFormat::UserProperty + 1;
constexpr int kSourceX    = QTextFormat::UserProperty + 2;
constexpr int kSourceY    = QTextFormat::UserProperty + 3;
constexpr int kSourceRise = QTextFormat::UserProperty + 4;

QTextCharFormat sourceFormat(const TextLayout::Source &source)
{
    QTextCharFormat format;
    format.setProperty(kSourcePage, source.page);
    format.setProperty(kSourceX, source.at.x());
    format.setProperty(kSourceY, source.at.y());
    format.setProperty(kSourceRise, source.rise);
    return format;
}

}

QList<TextLayout::Source> InlineEditor::charSources() const
{
    QList<TextLayout::Source> out;
    bool any = false;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        if (block != document()->begin()) out.append(TextLayout::Source{});
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QTextCharFormat format = fragment.charFormat();
            TextLayout::Source source;
            if (format.hasProperty(kSourcePage)) {
                source = { format.intProperty(kSourcePage),
                           QPointF(format.doubleProperty(kSourceX), format.doubleProperty(kSourceY)),
                           format.doubleProperty(kSourceRise) };
                any = true;
            }
            for (int k = 0; k < fragment.length(); ++k) out.append(source);
        }
    }
    return any ? out : QList<TextLayout::Source>();
}

void InlineEditor::setCharSources(const QList<TextLayout::Source> &sources)
{
    QTextCursor cursor(document());
    for (qsizetype i = 0; i < sources.size(); ++i) {
        if (!sources.at(i).isValid()) continue;
        cursor.setPosition(int(i));
        cursor.setPosition(int(i) + 1, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(sourceFormat(sources.at(i)));
    }
    m_metricsStale = true;
    relayout();
}

QMimeData *InlineEditor::createMimeDataFromSelection() const
{
    const QTextCursor cursor = textCursor();
    return mimeOf(cursor.selectionStart(), cursor.selectionEnd(),
                  laidBox(cursor.selectionStart(), cursor.selectionEnd()));
}

QMimeData *InlineEditor::mimeOf(int start, int end, const SourcedText::Box &box) const
{
    auto *mime = new QMimeData;
    QTextCursor cursor(document());
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, u'\n').replace(QChar::LineSeparator, u'\n');
    mime->setText(text);
    // Every character names one of the document with its style, so pasting
    // elsewhere keeps the font.
    const QList<TextLayout::Source> own = charSources();
    QList<TextLayout::Source> sources;
    for (int p = start; p < end; ++p) {
        TextLayout::Source source = own.value(p);
        if (!source.isValid()) source = m_metrics.styleSources.value(m_styles.value(p, -1));
        sources.append(source);
    }
    SourcedText::write(mime, m_documentKey, sources, box);
    return mime;
}

SourcedText::Box InlineEditor::laidBox(int start, int end) const
{
    SourcedText::Box box;
    box.size = m_boundsPt;
    box.fontSizePt = m_sizePt;
    // A list's markers are not characters of the text; such a box is laid out anew.
    if (m_metricsText != toPlainText() || m_lines.isEmpty() || !m_advance) return box;
    const QList<TextLayout::Source> own = charSources();
    for (const TextLayout::Line &line : m_lines) {
        if (!line.prefix.isEmpty()) return { box.size, box.fontSizePt, {}, {} };
        TextLayout::OriginalLine laid;
        QList<TextLayout::Source> lineSources;
        laid.baseline = m_origin.y() + line.y;
        double top = laid.baseline, bottom = laid.baseline;
        const int from = qMax(line.start, start);
        const int to   = qMin(line.start + line.length, end);
        for (int i = from; i < to && i < m_metricsText.size(); ++i) {
            const QChar ch = m_metricsText.at(i);
            if (ch == u'\n' || ch == QChar(TextLayout::kSoftBreak)) continue;
            const double size = m_metrics.sizes.value(m_styles.value(i, -1), m_sizePt);
            laid.text += ch;
            laid.charX.append(m_origin.x() + line.x + advanceTo(line, i));
            laid.objectStart.append(false);
            top    = qMin(top, laid.baseline - size * 0.8);
            bottom = qMax(bottom, laid.baseline + size * 0.25);
            TextLayout::Source source = own.value(i);
            if (!source.isValid()) source = m_metrics.styleSources.value(m_styles.value(i, -1));
            lineSources.append(source);
        }
        if (laid.text.trimmed().isEmpty()) continue;
        laid.endX = m_origin.x() + line.x + advanceTo(line, to);
        laid.rect = QRectF(QPointF(laid.charX.first(), top), QPointF(laid.endX, bottom));
        box.lines.append(laid);
        box.sources.append(lineSources);
    }
    // Part of the box: a box of its own around the characters marked.
    if (start > 0 || end < document()->characterCount() - 1)
        return SourcedText::boxAround(box.lines, box.sources, box.fontSizePt);
    return box;
}

void InlineEditor::copyAll()
{
    // Nothing marked: the whole box, with where each character is set in it.
    const int end = document()->characterCount() - 1;
    QApplication::clipboard()->setMimeData(mimeOf(0, end, laidBox(0, end)));
}

void InlineEditor::insertFromMimeData(const QMimeData *source)
{
    const QString text = source->text();
    if (text.isEmpty()) return;
    const QList<TextLayout::Source> sources = SourcedText::read(source, m_documentKey);
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    if (sources.isEmpty()) {
        // Text from elsewhere is typed in, in the style before it.
        cursor.insertText(text);
    } else {
        QTextCharFormat plain = cursor.charFormat();
        for (const int property : { kSourcePage, kSourceX, kSourceY, kSourceRise })
            plain.clearProperty(property);
        for (qsizetype i = 0; i < text.size();) {
            qsizetype end = i + 1;
            while (end < text.size() && sources.at(end) == sources.at(i)) ++end;
            QTextCharFormat format = plain;
            if (sources.at(i).isValid()) format.merge(sourceFormat(sources.at(i)));
            cursor.insertText(text.mid(i, end - i), format);
            i = end;
        }
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
}
