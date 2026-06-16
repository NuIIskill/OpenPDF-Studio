#include "InlineEditor.hpp"

#include <QKeyEvent>
#include <QFocusEvent>

InlineEditor::InlineEditor(QWidget *parent)
    : QTextEdit(parent)
{
    setObjectName(QStringLiteral("InlineEditor"));
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrapMode(QTextOption::NoWrap);
    setLineWrapMode(QTextEdit::NoWrap);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setContextMenuPolicy(Qt::NoContextMenu);
}

void InlineEditor::present(const QString &text, const QRectF &canvasBounds,
                            int pixelFontSize)
{
    // Style: white background, blue outline, matching font size
    setStyleSheet(QString(
        "QTextEdit#InlineEditor {"
        "  background: #FFFDE7;"
        "  border: 2px solid #3B82F6;"
        "  font-size: %1px;"
        "  padding: 0px 3px;"
        "  color: #111;"
        "}").arg(qMax(8, pixelFontSize)));

    // Make at least as wide as the original text run
    QRect geo = canvasBounds.toAlignedRect();
    geo.setWidth(qMax(geo.width(), 120));
    geo.adjust(-2, -2, 2, 2);
    setGeometry(geo);

    setPlainText(text);
    selectAll();
    show();
    raise();
    setFocus();
}

void InlineEditor::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        Q_EMIT cancelled();
        return;
    }
    // Enter without Shift commits; Shift+Enter inserts a newline
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
        && !(e->modifiers() & Qt::ShiftModifier)) {
        if (!m_committing) {
            m_committing = true;
            Q_EMIT committed(toPlainText());
        }
        return;
    }
    QTextEdit::keyPressEvent(e);
}

void InlineEditor::focusOutEvent(QFocusEvent *e)
{
    QTextEdit::focusOutEvent(e);
    if (!m_committing) {
        m_committing = true;
        Q_EMIT committed(toPlainText());
    }
}
