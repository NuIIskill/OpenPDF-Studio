#include "ui/edit/InlineEditor.hpp"

#include <QTextCursor>

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

TextLayout::Appearance InlineEditor::caretAppearance() const
{
    // What typing at the caret takes: the character before it, at the start
    // of a paragraph the one after it; with a selection its first character.
    const QTextCursor c = textCursor();
    if (c.hasSelection()) return appearanceOf(c.selectionStart());
    const int at = c.position();
    if (at > 0 && at <= m_metricsText.size() && m_metricsText.at(at - 1) != u'\n')
        return appearanceOf(at - 1);
    return appearanceOf(at);
}

void InlineEditor::setLookAt(int index)
{
    m_lookAt = index;
    Q_EMIT lookChanged();
}

TextLayout::Appearance InlineEditor::look() const
{
    // A clicked character is shown until the caret moves.
    return m_lookAt >= 0 ? appearanceOf(m_lookAt) : caretAppearance();
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
