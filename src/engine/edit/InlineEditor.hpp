#pragma once

#include <QTextEdit>

// A frameless inline text editor that sits directly over a PDF text run.
// Pressing Enter commits; Escape cancels; focus loss commits.
class InlineEditor : public QTextEdit
{
    Q_OBJECT

public:
    explicit InlineEditor(QWidget *parent = nullptr);

    // Position and size the editor over a canvas-coordinate rect.
    // pixelFontSize is estimated from the text run's rendered height.
    void present(const QString &text, const QRectF &canvasBounds,
                 int pixelFontSize);

Q_SIGNALS:
    void committed(const QString &text);
    void cancelled();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;

public:
    void resetCommitGuard() { m_committing = false; }

private:
    bool m_committing { false };
};
