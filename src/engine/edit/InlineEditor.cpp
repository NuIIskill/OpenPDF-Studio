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
    connect(this, &QTextEdit::textChanged, this, [this]() {
        Q_EMIT changed(toPlainText());
    });
}

static QString editorStyleSheet(int pixelFontSize, const QColor &color)
{
    return QString(
        "QTextEdit#InlineEditor {"
        "  background: transparent;"
        "  border: none;"
        "  font-size: %1px;"
        "  padding: 2px 4px;"
        "  color: %2;"
        "}").arg(qMax(8, pixelFontSize)).arg(color.name());
}

void InlineEditor::present(const QString &text, int pixelFontSize, const QColor &color)
{
    m_currentColor = color.isValid() ? color : QColor(0x11, 0x11, 0x11);
    setStyleSheet(editorStyleSheet(pixelFontSize, m_currentColor));
    setPlainText(text);
    selectAll();
    show();
}

void InlineEditor::setFontSize(int pixelFontSize)
{
    setStyleSheet(editorStyleSheet(pixelFontSize, m_currentColor));
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
    // Delete key with the full text selected → erase the block entirely.
    if (e->key() == Qt::Key_Delete) {
        const QTextCursor c = textCursor();
        const int selStart = qMin(c.anchor(), c.position());
        const int selEnd   = qMax(c.anchor(), c.position());
        if (selStart == 0 && selEnd == toPlainText().length() && selEnd > 0) {
            if (!m_committing) {
                m_committing = true;
                Q_EMIT committed(QString());
            }
            return;
        }
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
    if (m_suppressFocusOut || m_dragMode) {
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
