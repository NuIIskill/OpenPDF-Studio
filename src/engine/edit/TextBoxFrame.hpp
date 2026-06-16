#pragma once
#include <QWidget>

// Transparent overlay drawn on top of InlineEditor.
// Renders a blue dashed selection border with 8 resize handles.
class TextBoxFrame : public QWidget
{
    Q_OBJECT
public:
    explicit TextBoxFrame(QWidget *parent = nullptr);
    void presentAround(const QRectF &canvasBounds);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    static constexpr int kHandle = 8;
    static constexpr int kPad    = 8;
};
