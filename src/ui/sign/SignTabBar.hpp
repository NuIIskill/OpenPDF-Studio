#pragma once

#include <QTabBar>

/// Tab bar of the sign dialog, painted with icon and label centred together.
class SignTabBar : public QTabBar
{
public:
    explicit SignTabBar(QWidget *parent = nullptr);

    void addIconTab(const QString &icon, const QString &text);

protected:
    QSize tabSizeHint(int index) const override;
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int m_hovered { -1 };
};
