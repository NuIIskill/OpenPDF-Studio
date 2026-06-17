#pragma once
#include <QList>
#include <QRect>
#include <QWidget>

class InlineEditor;

// Interactive text-box widget: white background covers original page text,
// dashed blue border, 8 draggable resize handles at corners and edge midpoints.
// InlineEditor is a child widget filling the inner area.
class TextBoxFrame : public QWidget
{
    Q_OBJECT
public:
    explicit TextBoxFrame(QWidget *parent = nullptr);

    // decorations=false → no border/handles, editor fills exact area (direct-edit mode).
    // decorations=true  → dashed border + resize handles (new text-box mode).
    void setDecorations(bool on);

    // Show the frame.  canvasBounds = inner editor area in canvas (pixel) coords.
    void present(const QString &text, const QRectF &canvasBounds, int fontSize);
    // Live font size update (called while editor is active).
    void setFontSize(int pixelFontSize);
    // Rects (canvas coords) the frame must not overlap during resize/drag.
    void setForbiddenZones(const QList<QRect> &zones);
    void resetCommitGuard();

Q_SIGNALS:
    void committed(const QString &text);
    void cancelled();
    // Emitted continuously while the user drags a resize handle.
    // inner = inner editor rect in canvas coords (parent-widget space).
    void boundsChanged(QRectF inner);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    static constexpr int kH   = 8;   // visual handle size (px)
    static constexpr int kPad = 14;  // inner margin = hit-zone size; must be ≥ kH + 4
                                     // so the InlineEditor child never overlaps the handle zone

    enum class Handle { None, N, NE, E, SE, S, SW, W, NW };

    Handle hitTest(const QPoint &pos) const;
    void   applyCursor(Handle h);
    QRect  innerRect() const;

    InlineEditor *m_editor        { nullptr };
    Handle        m_drag          { Handle::None };
    QPoint        m_dragOrigin;
    QRect         m_dragStartGeo;
    bool          m_decorations   { true };
    QList<QRect>  m_forbidden;   // canvas-coord rects the frame must not overlap
};
