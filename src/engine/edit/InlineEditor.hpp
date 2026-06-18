#pragma once
#include <QColor>
#include <QTextEdit>

// Frameless text editor living inside TextBoxFrame.
// TextBoxFrame owns geometry; this widget only handles text and keyboard input.
// Enter commits, Escape cancels, focus-loss commits.
class InlineEditor : public QTextEdit
{
    Q_OBJECT
public:
    explicit InlineEditor(QWidget *parent = nullptr);

    // Set content, font size, and text color, then grab focus.
    void present(const QString &text, int pixelFontSize,
                 const QColor &color = QColor(0x11, 0x11, 0x11));
    // Change font size live without resetting content (color is preserved).
    void setFontSize(int pixelFontSize);

Q_SIGNALS:
    void committed(const QString &text);
    void cancelled();
    void changed(const QString &text);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;

public:
    void resetCommitGuard()    { m_committing = false; }
    void suppressNextFocusOut() { m_suppressFocusOut = true; }
    // Suppress ALL focus-out commits while a resize/move drag is in progress.
    void setDragMode(bool on)   { m_dragMode = on; }

private:
    bool   m_committing      { false };
    bool   m_suppressFocusOut{ false };
    bool   m_dragMode        { false };
    QColor m_currentColor    { 0x11, 0x11, 0x11 };
};
