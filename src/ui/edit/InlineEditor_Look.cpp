#include "ui/edit/InlineEditor.hpp"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>

namespace {

// What the user set on some characters, kept in their format so it moves with the text.
constexpr int kMarkFamily    = QTextFormat::UserProperty + 10;
constexpr int kMarkBold      = QTextFormat::UserProperty + 11;
constexpr int kMarkItalic    = QTextFormat::UserProperty + 12;
constexpr int kMarkUnderline = QTextFormat::UserProperty + 13;
constexpr int kMarkScale     = QTextFormat::UserProperty + 14;
constexpr int kMarkColor     = QTextFormat::UserProperty + 15;

std::optional<bool> stateOf(const QTextCharFormat &format, int property)
{
    if (!format.hasProperty(property)) return std::nullopt;
    return format.boolProperty(property);
}

QTextCharFormat markFormat(const TextLayout::Mark &mark)
{
    QTextCharFormat format;
    if (!mark.family.isEmpty())  format.setProperty(kMarkFamily, mark.family);
    if (mark.bold)               format.setProperty(kMarkBold, *mark.bold);
    if (mark.italic)             format.setProperty(kMarkItalic, *mark.italic);
    if (mark.underline)          format.setProperty(kMarkUnderline, *mark.underline);
    if (mark.scale > 0.0)        format.setProperty(kMarkScale, mark.scale);
    if (mark.color.isValid())    format.setProperty(kMarkColor, mark.color);
    return format;
}

}

int InlineEditor::charAt(const QPoint &viewportPos) const
{
    // The character under the point, not the gap between two nearest to it.
    const int position = positionAt(viewportPos);
    if (m_lines.isEmpty()) return position;
    const double xPt = (viewportPos.x() - m_pixelOffset.x()) / m_scale;
    return xPt < caretPointPt(position).x() ? position - 1 : position;
}

TextLayout::Appearance InlineEditor::appearanceOf(int index) const
{
    if (m_metricsText != toPlainText()) return {};
    // A paragraph break has no look of its own; the character before it has.
    while (index > 0 && (index >= m_metricsText.size() || m_metricsText.at(index) == u'\n'))
        --index;
    if (index < 0 || index >= m_styles.size()) return {};
    return m_metrics.appearances.value(m_styles.at(index));
}

int InlineEditor::lookIndex() const
{
    // A clicked character is shown until the caret moves.
    if (m_lookAt >= 0) return m_lookAt;
    // What typing at the caret takes: the character before it, at the start
    // of a paragraph the one after it; with a selection its first character.
    const QTextCursor c = textCursor();
    if (c.hasSelection()) return c.selectionStart();
    const int at = c.position();
    if (at > 0 && at <= m_metricsText.size() && m_metricsText.at(at - 1) != u'\n')
        return at - 1;
    return at;
}

void InlineEditor::setLookAt(int index)
{
    m_lookAt = index;
    Q_EMIT lookChanged();
}

TextLayout::Appearance InlineEditor::look() const
{
    return appearanceOf(lookIndex());
}

TextLayout::Mark InlineEditor::lookMark() const
{
    const QString text = toPlainText();
    int index = lookIndex();
    while (index > 0 && (index >= text.size() || text.at(index) == u'\n')) --index;
    return charMarks().value(index);
}

QList<TextLayout::Appearance> InlineEditor::appearances() const
{
    QList<TextLayout::Appearance> out;
    if (m_metricsText != toPlainText()) return out;
    for (qsizetype i = 0; i < m_metricsText.size() && i < m_styles.size(); ++i) {
        if (m_metricsText.at(i).isSpace()) continue;
        const TextLayout::Appearance look = m_metrics.appearances.value(m_styles.at(i));
        if (look.isValid() && !out.contains(look)) out.append(look);
    }
    return out;
}

QList<TextLayout::Mark> InlineEditor::charMarks() const
{
    QList<TextLayout::Mark> out;
    bool any = false;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        if (block != document()->begin()) out.append(out.isEmpty() ? TextLayout::Mark{} : out.last());
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QTextCharFormat format = fragment.charFormat();
            TextLayout::Mark mark;
            mark.family = format.stringProperty(kMarkFamily);
            mark.bold      = stateOf(format, kMarkBold);
            mark.italic    = stateOf(format, kMarkItalic);
            mark.underline = stateOf(format, kMarkUnderline);
            mark.scale     = format.doubleProperty(kMarkScale);
            mark.color     = format.colorProperty(kMarkColor);
            any |= !mark.isEmpty();
            for (int k = 0; k < fragment.length(); ++k) out.append(mark);
        }
    }
    return any ? out : QList<TextLayout::Mark>();
}

void InlineEditor::remark(int start, const QList<TextLayout::Mark> &marks)
{
    const QList<TextLayout::Mark> had = charMarks();
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    // The look is read again for the new marks, even where the text is the same.
    m_metricsText.clear();
    for (int k = 0; k < marks.size(); ++k) {
        const int i = start + k;
        if (had.value(i) == marks.at(k)) continue;
        cursor.setPosition(i);
        cursor.setPosition(i + 1, QTextCursor::KeepAnchor);
        QTextCharFormat format = cursor.charFormat();
        for (const int property : { kMarkFamily, kMarkBold, kMarkItalic, kMarkUnderline,
                                    kMarkScale, kMarkColor })
            format.clearProperty(property);
        format.merge(markFormat(marks.at(k)));
        cursor.setCharFormat(format);
    }
    cursor.endEditBlock();
    relayout();
    viewport()->update();
    Q_EMIT lookChanged();
}

void InlineEditor::setCharMarks(const QList<TextLayout::Mark> &marks)
{
    remark(0, marks.mid(0, document()->characterCount() - 1));
}

bool InlineEditor::markSelection(const TextLayout::Mark &mark)
{
    const QTextCursor c = textCursor();
    if (!c.hasSelection()) return false;
    const QList<TextLayout::Mark> had = charMarks();
    QList<TextLayout::Mark> marks;
    for (int i = c.selectionStart(); i < c.selectionEnd(); ++i) marks.append(had.value(i).over(mark));
    remark(c.selectionStart(), marks);
    return true;
}

void InlineEditor::unmark(const TextLayout::Mark &fields)
{
    QList<TextLayout::Mark> marks = charMarks();
    if (marks.isEmpty()) return;
    for (TextLayout::Mark &m : marks) m = m.without(fields);
    remark(0, marks);
}
