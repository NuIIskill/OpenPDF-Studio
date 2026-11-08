#pragma once

#include <QPointer>
#include <QTransform>
#include <QWidget>

#include <functional>

class TextBoxFrame;

/// The edit frame of turned text drawn turned on the text, which moves and resizes it through the upright frame.
class TurnedFrame : public QWidget
{
public:
    explicit TurnedFrame(TextBoxFrame *frame);

    void place(const QTransform &turn, double angle);
    void leave();
    void setOnDrag(std::function<void(bool)> onDrag) { m_onDrag = std::move(onDrag); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QTransform toCanvas() const;
    void follow();
    void pass(QMouseEvent *event);

    TextBoxFrame     *m_frame { nullptr };
    QTransform        m_turn;
    double            m_angle { 0.0 };
    QPointer<QWidget> m_held;
    bool              m_dragging { false };
    std::function<void(bool)> m_onDrag;
};
