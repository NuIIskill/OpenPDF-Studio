#pragma once

#include <QImage>
#include <QPolygonF>
#include <QRectF>
#include <QWidget>

/// The card around the upright box that edits turned text, the arrow from it to that text and how the text is turned.
class EditCallout : public QWidget
{
public:
    explicit EditCallout(QWidget *parent);

    void point(const QRectF &box, const QPolygonF &target, double turn, const QImage &text);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QRectF chipRect() const;

    QRectF    m_box;
    QPolygonF m_target;
    double    m_turn { 0.0 };
    QImage    m_text;
};
