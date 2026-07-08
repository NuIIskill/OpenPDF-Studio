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

void InlineEditor::applyStyle()
{
    // Strip quote characters from the family so it can't break the stylesheet.
    QString family = m_family;
    family.remove(u'\'').remove(u'"');
    const QString familyList = family.isEmpty()
        ? QStringLiteral("Helvetica, Arial, 'Liberation Sans', sans-serif")
        : QStringLiteral("'%1', Helvetica, Arial, 'Liberation Sans', sans-serif")
              .arg(family);
    setStyleSheet(QString(
        "QTextEdit#InlineEditor {"
        "  background: transparent;"
        "  border: none;"
        "  font-family: %1;"
        "  font-size: %2px;"
        "  font-weight: %3;"
        "  font-style: %4;"
        "  padding: 2px 4px;"
        "  color: %5;"
        "}")
        .arg(familyList)
        .arg(qMax(8, m_currentFontPx))
        .arg(m_bold ? 700 : 400)
        .arg(m_italic ? QStringLiteral("italic") : QStringLiteral("normal"))
        .arg(m_currentColor.name()));
}

QFont InlineEditor::styledFont(int pixelFontSize) const
{
    QFont f(m_family.isEmpty() ? QStringLiteral("Helvetica") : m_family);
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(qMax(8, pixelFontSize));
    f.setBold(m_bold);
    f.setItalic(m_italic);
    return f;
}

void InlineEditor::present(const QString &text, int pixelFontSize, const QColor &color,
                           const QString &family, bool bold, bool italic)
{
    m_currentColor  = color.isValid() ? color : QColor(0x11, 0x11, 0x11);
    m_currentFontPx = qMax(8, pixelFontSize);
    m_family        = family;
    m_bold          = bold;
    m_italic        = italic;
    applyStyle();
    setPlainText(text);
    selectAll();
    show();
}

void InlineEditor::setFontSize(int pixelFontSize)
{
    m_currentFontPx = qMax(8, pixelFontSize);
    applyStyle();
}

void InlineEditor::setColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_currentColor = color;
    applyStyle();
}

void InlineEditor::setTextFont(const QString &family, bool bold, bool italic)
{
    m_family = family;
    m_bold   = bold;
    m_italic = italic;
    applyStyle();
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
