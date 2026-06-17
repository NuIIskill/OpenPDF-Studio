#pragma once
#include <QTextEdit>

// Frameless text editor living inside TextBoxFrame.
// TextBoxFrame owns geometry; this widget only handles text and keyboard input.
// Enter commits, Escape cancels, focus-loss commits.
class InlineEditor : public QTextEdit
{
    Q_OBJECT
public:
    explicit InlineEditor(QWidget *parent = nullptr);

    // Set content and font size, then grab focus.
    void present(const QString &text, int pixelFontSize);
    // Change font size live without resetting content.
    void setFontSize(int pixelFontSize);

Q_SIGNALS:
    void committed(const QString &text);
    void cancelled();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;

public:
    void resetCommitGuard()    { m_committing = false; }
    void suppressNextFocusOut() { m_suppressFocusOut = true; }

private:
    bool m_committing     { false };
    bool m_suppressFocusOut { false };
};
