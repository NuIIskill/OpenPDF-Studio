#pragma once
#include <QColor>
#include <QList>
#include <QRect>
#include <QWidget>

#include "engine/edit/TextBoxProperties.hpp"

class InlineEditor;

// Provides the movable and resizable frame around an InlineEditor.
class TextBoxFrame : public QWidget
{
    Q_OBJECT
public:
    explicit TextBoxFrame(QWidget *parent = nullptr);

    void setDecorations(bool on);
    void present(const QString &text, const QRectF &canvasBounds, qreal fontSize,
                 const QColor &color = QColor(0x11, 0x11, 0x11),
                 const QString &fontFamily = QString(),
                 bool bold = false, bool italic = false);
    void setFontSize(qreal pixelFontSize);
    void setTextColor(const QColor &color);
    // Change font family/style live (size, color, content preserved).
    void setTextFont(const QString &family, bool bold, bool italic);
    // Reposition and resize for a new zoom level without disturbing the editor's
    // current text or cursor selection.
    void repositionForZoom(const QRectF &canvasBounds, qreal pixelFontSize,
                           const TextBoxProperties &box, qreal scale);
    void setTextAnchor(bool valid, const QPointF &penOffsetPt);
    void setForbiddenZones(const QList<QRect> &zones);
    // Clamp drag/resize to this rect (canvas coords). Pass null rect to disable clamping.
    void setPageRect(const QRect &pageRect);
    void resetCommitGuard();
    QString currentText() const;
    // Returns the inner editing rect in parent (canvas) coordinates.
    QRectF innerCanvasRect() const;
    // Grow the frame until the whole document is visible. Height always
    // grows; width grows too for single-line edits (setGrowHorizontal) so
    // typing extends the line instead of wrapping it — a wrap the commit
    // would paint into the document.
    void growToFitText();
    void setGrowHorizontal(bool on);
    void setBoxProperties(const TextBoxProperties &properties, qreal scale);

Q_SIGNALS:
    void committed(const QString &text);
    void cancelled();
    void changed(const QString &text);
    void dragEnded();
    void boundsChanged(QRectF inner);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void moveEvent(QMoveEvent *) override;

private:
    static constexpr int kH   = 8;
    static constexpr int kPad = 14;

    enum class Handle { None, Move, NE, E, SE, S, SW, W, NW, N };

    Handle hitTest(const QPoint &pos) const;
    void   applyCursor(Handle h);
    QRect  innerRect() const;
    void   layoutEditor();
    // Minimum inner height for a given font pixel size (see the .cpp).
    static int minInnerHeight(qreal fontPixelSize);

    InlineEditor *m_editor      { nullptr };
    Handle        m_drag        { Handle::None };
    QPoint        m_dragOrigin;
    QRect         m_dragStartGeo;
    bool          m_decorations { true };
    bool          m_presenting  { false };  // suppress boundsChanged during present()
    // Minimum INNER width. Only drag-created (empty) boxes get the large
    // minimum — in-place edits must keep the width of the original text so
    // the tracked edit bounds stay honest.
    int           m_minInnerW   { 120 };
    bool          m_growHorizontal { false };
    bool          m_autoHeight { true };
    TextBoxProperties m_box;
    qreal         m_scale { 1.0 };
    QPointF       m_anchorPt;
    bool          m_hasAnchor { false };
    QList<QRect>  m_forbidden;
    QRect         m_pageRect;              // empty = no clamping
};
