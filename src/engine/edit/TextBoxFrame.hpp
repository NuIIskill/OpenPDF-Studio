#pragma once
#include <QColor>
#include <QList>
#include <QRect>
#include <QWidget>

class InlineEditor;

class TextBoxFrame : public QWidget
{
    Q_OBJECT
public:
    explicit TextBoxFrame(QWidget *parent = nullptr);

    void setDecorations(bool on);
    void present(const QString &text, const QRectF &canvasBounds, int fontSize,
                 const QColor &color = QColor(0x11, 0x11, 0x11));
    void setFontSize(int pixelFontSize);
    void setForbiddenZones(const QList<QRect> &zones);
    // Clamp drag/resize to this rect (canvas coords). Pass null rect to disable clamping.
    void setPageRect(const QRect &pageRect);
    void resetCommitGuard();
    QString currentText() const;

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

    InlineEditor *m_editor      { nullptr };
    Handle        m_drag        { Handle::None };
    QPoint        m_dragOrigin;
    QRect         m_dragStartGeo;
    bool          m_decorations { true };
    bool          m_presenting  { false };  // suppress boundsChanged during present()
    QList<QRect>  m_forbidden;
    QRect         m_pageRect;              // empty = no clamping
};
