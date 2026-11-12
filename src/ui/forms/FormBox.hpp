#pragma once

#include <QWidget>

class FormLayer;

/// The place of one field widget on the canvas, where it is clicked to be filled in.
class FormBox : public QWidget
{
public:
    FormBox(FormLayer *layer, int index, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    FormLayer *m_layer;
    int        m_index;
    bool       m_hover { false };
};
