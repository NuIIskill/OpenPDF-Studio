#pragma once
#include <QColor>
#include <QHash>
#include <QFont>
#include <QTextEdit>

#include <functional>

QT_BEGIN_NAMESPACE
class QPainter;
class QTextBlock;
class QTextLine;
class QTimer;
QT_END_NAMESPACE

#include "engine/edit/TextBoxProperties.hpp"

/// Provides inline text editing inside a TextBoxFrame.
class InlineEditor : public QTextEdit
{
    Q_OBJECT
public:
    explicit InlineEditor(QWidget *parent = nullptr);

    void present(const QString &text, qreal pixelFontSize,
                 const QColor &color = QColor(0x11, 0x11, 0x11),
                 const QString &family = QString(),
                 bool bold = false, bool italic = false,
                 bool underline = false);

    void setFontSize(int pixelFontSize);

    void setColor(const QColor &color);

    void setTextFont(const QString &family, bool bold, bool italic,
                     bool underline);

    void setStandardFace(bool on);
    void setBoxProperties(const TextBoxProperties &properties, qreal scale);

    QFont styledFont(qreal pixelFontSize) const;
    qreal screenDpi() const;
    qreal firstBaselineOffset() const;
    QString laidOutText() const;
    int   fontPixelSize() const { return qRound(m_currentFontPx); }
    qreal fontPixelSizeF() const { return m_currentFontPx; }
    void setGlyphsVisible(bool on);
    void setLineSpacingPt(qreal pt);
    qreal lineSpacingPt() const { return m_lineSpacingPt; }
    qreal contentWidthPt() const;
    void setCaretVisible(bool on);
    void setAdvanceMeasure(std::function<double(const QString &)> measure);

    double advancePt(QStringView text) const;

    int    engineLineCount(qreal widthPt) const;
    bool glyphsVisible() const { return m_glyphs; }
    void  setFontSizeF(qreal pixelFontSize);

Q_SIGNALS:
    void committed(const QString &text);
    void cancelled();
    void changed(const QString &text);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

public:
    void resetCommitGuard()    { m_committing = false; }
    void suppressNextFocusOut() { m_suppressFocusOut = true; }

    void setDragMode(bool on)   { m_dragMode = on; }

private:
    int    positionAt(const QPoint &viewportPos) const;
    double charWidth(QChar ch) const;

    qreal engineX(const QTextBlock &block, const QTextLine &line,
                  int posInBlock) const;
    void  paintSelection(QPainter &p) const;
    void applyStyle();
    void applyParagraphSpacing();
    void updateVerticalAlignment();

    bool    m_committing      { false };
    bool    m_suppressFocusOut{ false };
    bool    m_dragMode        { false };
    QColor  m_currentColor    { 0x11, 0x11, 0x11 };
    qreal   m_currentFontPx   { 12.0 };
    bool    m_glyphs          { false };
    qreal   m_lineSpacingPt   { 0.0 };
    std::function<double(const QString &)> m_advance;

    mutable QHash<uint, double> m_charWidth;
    bool    m_caretPinned     { false };
    QString m_lastText;
    bool    m_caretOn         { true };
    QTimer *m_caretTimer      { nullptr };
    QString m_family;
    bool    m_bold            { false };
    bool    m_italic          { false };
    bool    m_underline       { false };
    bool    m_standardFace    { false };
    TextBoxProperties m_box;
    qreal   m_scale           { 1.0 };
};
