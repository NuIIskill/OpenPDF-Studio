#include "InlineEditor.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QFocusEvent>

InlineEditor::InlineEditor(QWidget *parent)
    : QTextEdit(parent)
{
    setObjectName(QStringLiteral("InlineEditor"));
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setContextMenuPolicy(Qt::NoContextMenu);
}

static QString editorStyleSheet(int pixelFontSize)
{
    return QString(
        "QTextEdit#InlineEditor {"
        "  background: transparent;"
        "  border: none;"
        "  font-size: %1px;"
        "  padding: 2px 4px;"
        "  color: #111;"
        "}").arg(qMax(8, pixelFontSize));
}

void InlineEditor::present(const QString &text, int pixelFontSize)
{
    setStyleSheet(editorStyleSheet(pixelFontSize));
    setPlainText(text);
    selectAll();
    show();
}

void InlineEditor::setFontSize(int pixelFontSize)
{
    setStyleSheet(editorStyleSheet(pixelFontSize));
}

void InlineEditor::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        Q_EMIT cancelled();
        return;
    }
    // Enter without Shift commits; Shift+Enter inserts a newline.
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
    QWidget *newFocus = qApp->focusWidget();
    qWarning() << "[IE] focusOutEvent reason=" << (int)e->reason()
               << "suppress=" << m_suppressFocusOut
               << "newFocus=" << (newFocus ? newFocus->metaObject()->className() : "null")
               << (newFocus ? newFocus->objectName() : "");
    if (m_suppressFocusOut) {
        m_suppressFocusOut = false;
        QTextEdit::focusOutEvent(e);
        return;   // spurious/transient focus loss — don't commit
    }
    QTextEdit::focusOutEvent(e);
    if (!m_committing) {
        m_committing = true;
        Q_EMIT committed(toPlainText());
    }
}
