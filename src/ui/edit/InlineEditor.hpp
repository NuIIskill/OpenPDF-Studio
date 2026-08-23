#pragma once
#include <QColor>
#include <QFont>
#include <QTextEdit>

#include "engine/edit/TextBoxProperties.hpp"

// Frameless text editor living inside TextBoxFrame.
// TextBoxFrame owns geometry; this widget only handles text and keyboard input.
// Enter inserts a newline; Escape cancels; focus-loss commits.
class InlineEditor : public QTextEdit
{
    Q_OBJECT
public:
    explicit InlineEditor(QWidget *parent = nullptr);

    // Set content, font size, and text color, then grab focus.
    void present(const QString &text, int pixelFontSize,
                 const QColor &color = QColor(0x11, 0x11, 0x11),
                 const QString &family = QString(),
                 bool bold = false, bool italic = false);
    // Change font size live without resetting content (color is preserved).
    void setFontSize(int pixelFontSize);
    // Change text color live without resetting content (font size is preserved).
    void setColor(const QColor &color);
    // Change font family/style live (size and color are preserved).
    void setTextFont(const QString &family, bool bold, bool italic);
    void setBoxProperties(const TextBoxProperties &properties, qreal scale);

    // The effective editor font at the given pixel size (for layout metrics).
    QFont styledFont(int pixelFontSize) const;
    int   fontPixelSize() const { return m_currentFontPx; }

Q_SIGNALS:
    void committed(const QString &text);
    void cancelled();
    void changed(const QString &text);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

public:
    void resetCommitGuard()    { m_committing = false; }
    void suppressNextFocusOut() { m_suppressFocusOut = true; }
    // Suppress ALL focus-out commits while a resize/move drag is in progress.
    void setDragMode(bool on)   { m_dragMode = on; }

private:
    void applyStyle();
    void applyParagraphSpacing();
    void updateVerticalAlignment();

    bool    m_committing      { false };
    bool    m_suppressFocusOut{ false };
    bool    m_dragMode        { false };
    QColor  m_currentColor    { 0x11, 0x11, 0x11 };
    int     m_currentFontPx   { 12 };
    QString m_family;
    bool    m_bold            { false };
    bool    m_italic          { false };
    TextBoxProperties m_box;
    qreal   m_scale           { 1.0 };
};
