#pragma once
#include <QColor>
#include <QList>
#include <QRect>
#include <QWidget>

#include <functional>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

#include "engine/edit/TextBoxProperties.hpp"

class InlineEditor;

/// Provides the movable and resizable frame around an InlineEditor.
class TextBoxFrame : public QWidget
{
    Q_OBJECT
public:
    explicit TextBoxFrame(QWidget *parent = nullptr);

    void setDecorations(bool on);
    void setGlyphsVisible(bool on);
    void setStandardFace(bool on);
    void setLineSpacingPt(qreal pt);
    void setAdvanceMeasure(std::function<double(const QString &)> measure);
    void present(const QString &text, const QRectF &canvasBounds, qreal fontSize,
                 const QColor &color = QColor(0x11, 0x11, 0x11),
                 const QString &fontFamily = QString(),
                 bool bold = false, bool italic = false,
                 bool underline = false);
    void setFontSize(qreal pixelFontSize);
    void setTextColor(const QColor &color);

    void setTextFont(const QString &family, bool bold, bool italic,
                     bool underline);

    void repositionForZoom(const QRectF &canvasBounds, qreal pixelFontSize,
                           const TextBoxProperties &box, qreal scale);
    void setTextAnchor(bool valid, const QPointF &penOffsetPt);
    void setForbiddenZones(const QList<QRect> &zones);

    void setPageRect(const QRect &pageRect);
    void resetCommitGuard();
    QString currentText() const;

    QRectF innerCanvasRect() const;

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

    bool eventFilter(QObject *watched, QEvent *event) override;
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
    int    handleSize() const;
    void   layoutEditor();
    void   applyBoxSize();

    qreal  freieBreitePt() const;

    qreal  textBreitePt(qreal boxBreitePt) const;

    static int minInnerHeight(qreal fontPixelSize);

    InlineEditor *m_editor      { nullptr };
    Handle        m_drag        { Handle::None };
    QPoint        m_dragOrigin;
    QRect         m_dragStartGeo;
    bool          m_decorations { true };
    bool          m_presenting  { false };

    int           m_minInnerW   { 120 };
    bool          m_growHorizontal { false };
    bool          m_autoHeight { true };
    TextBoxProperties m_box;
    qreal         m_scale { 1.0 };
    QSizeF        m_boxPt;

    bool          m_userSized { false };
    QPointF       m_anchorPt;
    bool          m_hasAnchor { false };

    QList<QRect>  m_forbidden;
    QRect         m_pageRect;
};
