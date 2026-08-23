#pragma once

#include "ui/theme/Theme.hpp"

#include <QAbstractButton>
#include <QPainter>

class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setCheckable(true);
        setFixedSize(44, 26);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const bool on = isChecked();
        const bool dk = Theme::DarkMode;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        p.setPen(Qt::NoPen);
        p.setBrush(on ? QColor(QStringLiteral("#2563EB"))
                      : QColor(dk ? QStringLiteral("#555555")
                                  : QStringLiteral("#D1D5DB")));
        p.drawRoundedRect(rect(), 13, 13);

        p.setBrush(Qt::white);
        const int kd = 20;
        const int ky = (height() - kd) / 2;
        const int kx = on ? (width() - kd - 2) : 2;
        p.drawEllipse(kx, ky, kd, kd);
    }
};
