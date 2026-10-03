#pragma once
#include <QColor>
#include <QFont>
#include <QList>
#include <QSet>
#include <QPointF>
#include <QSizeF>
#include <QTextEdit>

#include <functional>

QT_BEGIN_NAMESPACE
class QPainter;
class QTimer;
QT_END_NAMESPACE

#include "engine/edit/TextBoxProperties.hpp"
#include "engine/edit/TextLayout.hpp"

/// Provides inline text editing inside a TextBoxFrame.
class InlineEditor : public QTextEdit
{
    Q_OBJECT
public:
    using MetricsSource = std::function<TextLayout::Metrics(const QString &text)>;

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
    QString plainText() const;
    int   fontPixelSize() const { return qRound(m_currentFontPx); }
    qreal fontPixelSizeF() const { return m_currentFontPx; }
    void setGlyphsVisible(bool on);
    void setLineSpacingPt(qreal pt);
    qreal lineSpacingPt() const { return m_lineSpacingPt; }
    void setCaretVisible(bool on);
    bool glyphsVisible() const { return m_glyphs; }
    void  setFontSizeF(qreal pixelFontSize);

    void setMetricsSource(MetricsSource source);
    void invalidateMetrics();
    void setLayoutBox(const QSizeF &boundsPt, bool wraps);
    void setTextAnchor(bool valid, const QPointF &penOffsetPt);
    void setPixelOffset(const QPointF &offset);

    bool   usesEngineLayout() const;
    const QList<TextLayout::Line> &layoutLines() const { return m_lines; }
    QPointF layoutOriginPt() const { return m_origin; }
    double layoutSizePt() const { return m_metrics.sizePt; }
    double contentWidthPt() const;
    double contentHeightPt() const;

Q_SIGNALS:
    void committed(const QString &text);
    void cancelled();
    void changed(const QString &text);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void scrollContentsBy(int dx, int dy) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

public:
    void resetCommitGuard()    { m_committing = false; }
    void suppressNextFocusOut() { m_suppressFocusOut = true; }

    void setDragMode(bool on)   { m_dragMode = on; }

    int    lineOf(int position) const;
    QPointF caretPointPt(int position) const;

private:
    int    positionAt(const QPoint &viewportPos) const;
    int    positionOnLine(int line, double xPt) const;
    double advanceTo(const TextLayout::Line &line, int position) const;
    QRectF caretRectPx(int position) const;
    void   moveCursorTo(int position, bool keepAnchor);
    void   paintSelection(QPainter &p) const;
    void   paintCaret(QPainter &p) const;
    void   refreshMetrics();
    void   relayout();
    void   applyStyle();
    void   applyParagraphSpacing();
    void   updateVerticalAlignment();

    bool    m_committing      { false };
    bool    m_suppressFocusOut{ false };
    bool    m_dragMode        { false };
    QColor  m_currentColor    { 0x11, 0x11, 0x11 };
    qreal   m_currentFontPx   { 12.0 };
    bool    m_glyphs          { false };
    qreal   m_lineSpacingPt   { 0.0 };
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

    MetricsSource            m_source;
    TextLayout::Metrics      m_metrics;
    QString                  m_metricsText;
    QSet<uint>               m_metricsCharacters;
    bool                     m_metricsStale { true };
    QList<TextLayout::Line>  m_lines;
    QPointF                  m_origin;
    QSizeF                   m_boundsPt;
    bool                     m_wraps     { false };
    bool                     m_hasAnchor { false };
    QPointF                  m_anchorPt;
    double                   m_goalXPt   { -1.0 };
    QPointF                  m_pixelOffset;
};
