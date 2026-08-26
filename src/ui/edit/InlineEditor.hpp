#pragma once
#include <QColor>
#include <QFont>
#include <QTextEdit>

#include "engine/edit/TextBoxProperties.hpp"

// Provides inline text editing inside a TextBoxFrame.
class InlineEditor : public QTextEdit
{
    Q_OBJECT
public:
    explicit InlineEditor(QWidget *parent = nullptr);

    // Set content, font size, and text color, then grab focus.
    void present(const QString &text, qreal pixelFontSize,
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
    QFont styledFont(qreal pixelFontSize) const;
    qreal screenDpi() const;
    qreal firstBaselineOffset() const;
    QString laidOutText() const;
    int   fontPixelSize() const { return qRound(m_currentFontPx); }
    qreal fontPixelSizeF() const { return m_currentFontPx; }
    void  setFontSizeF(qreal pixelFontSize);

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
    qreal   m_currentFontPx   { 12.0 };
    QString m_family;
    bool    m_bold            { false };
    bool    m_italic          { false };
    TextBoxProperties m_box;
    qreal   m_scale           { 1.0 };
};
