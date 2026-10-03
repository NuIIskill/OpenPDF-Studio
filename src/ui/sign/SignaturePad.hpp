#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QPolygonF>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QPainter;
QT_END_NAMESPACE

/// Freehand surface on which the user draws a handwritten signature.
class SignaturePad : public QWidget
{
    Q_OBJECT

public:
    explicit SignaturePad(QWidget *parent = nullptr);

    void setPenColor(const QColor &color);
    void setPenWidth(qreal width);
    void clear();
    bool isEmpty() const { return m_strokes.isEmpty(); }
    QImage toImage(qreal scale) const;

Q_SIGNALS:
    void changed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void paintStrokes(QPainter &p) const;

    QList<QPolygonF> m_strokes;
    QColor m_color { QStringLiteral("#1D4ED8") };
    qreal  m_width { 2.0 };
    bool   m_drawing { false };
};
