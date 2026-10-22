#pragma once

#include <QTabBar>

/// Tab bar whose tabs show an icon and a label centred together.
class IconTabBar : public QTabBar
{
public:
    explicit IconTabBar(QWidget *parent = nullptr);

    void addIconTab(const QString &icon, const QString &text);

protected:
    QSize tabSizeHint(int index) const override;
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int m_hovered { -1 };
};
