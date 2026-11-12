#include "ui/forms/FormEditLayout.hpp"

#include "engine/forms/FormTextLayout.hpp"

#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <cmath>

FormEditLayout::FormEditLayout(QTextDocument *document)
    : QAbstractTextDocumentLayout(document)
{}

void FormEditLayout::setMetrics(qreal size, qreal scale, qreal width, int alignment, const QPointF &plate)
{
    if (qFuzzyCompare(size, m_size) && qFuzzyCompare(scale, m_scale) && qFuzzyCompare(width, m_width)
            && alignment == m_alignment && plate == m_plate)
        return;
    m_size      = size;
    m_scale     = scale;
    m_width     = width;
    m_alignment = alignment;
    m_plate     = plate;
    relayout();
}

void FormEditLayout::relayout()
{
    const qreal spacing = FormTextLayout::kLineSpacing * m_size * m_scale;
    // The editor covers the field inside its border, so positions are taken from there.
    qreal baseline = FormTextLayout::firstBaseline(m_size) * m_scale - m_plate.y();
    qreal bottom = 0;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        QTextLayout *layout = block.layout();
        // Any character may end a line, so each line takes exactly the characters PDFium gives it.
        QTextOption option = document()->defaultTextOption();
        option.setWrapMode(QTextOption::WrapAnywhere);
        layout->setTextOption(option);
        layout->setPosition(QPointF());
        layout->beginLayout();
        // Qt may take the spaces after a line with it, so each line is given up to PDFium's end.
        int start = 0, taken = 0;
        for (const int length : FormTextLayout::lineLengths(block.text(), m_width, m_size)) {
            const int end = start + length;
            if (taken < end || length == 0) {
                QTextLine line = layout->createLine();
                if (!line.isValid()) break;
                line.setNumColumns(end - taken);
                // PDFium aligns a line with the space it ends on.
                const qreal free = m_width - FormTextLayout::textWidth(block.text().mid(start, length), m_size);
                // Spaces PDFium begins this line with went to the line before; the rest keeps its place.
                const qreal lead = taken > start ? FormTextLayout::textWidth(block.text().mid(start, taken - start), m_size) : 0;
                const qreal x = (FormTextLayout::kInset + lead + qMax(0.0, free) * m_alignment / 2.0) * m_scale - m_plate.x();
                // Qt draws on the whole pixel below the baseline; PDFium's lands on the nearest one.
                line.setPosition(QPointF(x, std::round(baseline) + 0.001 - line.ascent()));
                bottom = baseline + line.descent();
                taken = line.textStart() + line.textLength();
            }
            baseline += spacing;
            start = end;
        }
        layout->endLayout();
        block.setLineCount(layout->lineCount());
    }
    m_height = bottom + FormTextLayout::kInset * m_scale;
    Q_EMIT documentSizeChanged(documentSize());
    Q_EMIT update();
}

void FormEditLayout::documentChanged(int, int, int)
{
    relayout();
}

void FormEditLayout::draw(QPainter *painter, const PaintContext &context)
{
    painter->setPen(context.palette.color(QPalette::Text));
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const int start = block.position(), length = block.length();
        QList<QTextLayout::FormatRange> selections;
        for (const Selection &s : context.selections) {
            const int from = s.cursor.selectionStart() - start, to = s.cursor.selectionEnd() - start;
            if (to <= 0 || from >= length) continue;
            QTextLayout::FormatRange range;
            range.start  = qMax(0, from);
            range.length = qMin(length, to) - range.start;
            range.format = s.format;
            selections << range;
        }
        block.layout()->draw(painter, QPointF(), selections, context.clip);
        // QTextEdit keeps its caret's width as a property of the layout.
        const QVariant caret = property("cursorWidth");
        if (context.cursorPosition >= start && context.cursorPosition < start + length)
            block.layout()->drawCursor(painter, QPointF(), context.cursorPosition - start,
                                       caret.isValid() ? caret.toInt() : 1);
    }
}

int FormEditLayout::hitTest(const QPointF &point, Qt::HitTestAccuracy) const
{
    int best = 0;
    qreal bestDistance = 1e12;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const QTextLayout *layout = block.layout();
        for (int i = 0; i < layout->lineCount(); ++i) {
            const QTextLine line = layout->lineAt(i);
            const qreal distance = point.y() < line.y() ? line.y() - point.y()
                                 : point.y() > line.y() + line.height() ? point.y() - line.y() - line.height() : 0;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = block.position() + line.xToCursor(point.x());
            }
        }
    }
    return best;
}

QSizeF FormEditLayout::documentSize() const
{
    return QSizeF(m_width * m_scale, m_height);
}

QRectF FormEditLayout::frameBoundingRect(QTextFrame *) const
{
    return QRectF(QPointF(), documentSize());
}

QRectF FormEditLayout::blockBoundingRect(const QTextBlock &block) const
{
    const QTextLayout *layout = block.layout();
    if (!layout || layout->lineCount() == 0) return {};
    const QTextLine first = layout->lineAt(0), last = layout->lineAt(layout->lineCount() - 1);
    return QRectF(0, first.y(), documentSize().width(), last.y() + last.height() - first.y());
}
