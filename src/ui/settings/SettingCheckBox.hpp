#pragma once

#include "ui/theme/Theme.hpp"

#include <QAbstractButton>
#include <QPainter>
#include <QPainterPath>

// Painted by hand for the same reason ToggleSwitch is: a style-sheet indicator
// would need a bitmap check mark, and the app deliberately ships without Qt SVG.

class SettingCheckBox : public QAbstractButton
{
    Q_OBJECT
public:
    explicit SettingCheckBox(const QString &text, QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setCheckable(true);
        setText(text);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    static constexpr int kBox     = 20;   // indicator edge length
    static constexpr int kSpacing = 14;   // indicator → label gap

    [[nodiscard]] QSize sizeHint() const override
    {
        const QFontMetrics fm(labelFont());
        return { kBox + kSpacing + fm.horizontalAdvance(text()),
                 qMax(kBox, fm.height()) };
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const bool on = isChecked();
        const bool dk = Theme::DarkMode;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF box(0.5, (height() - kBox) / 2.0 + 0.5, kBox - 1, kBox - 1);
        if (on) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#2563EB")));
            p.drawRoundedRect(box, 5, 5);

            QPen check(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(check);
            const qreal x = box.left(), y = box.top(), w = box.width(), h = box.height();
            QPainterPath path;
            path.moveTo(x + w * 0.26, y + h * 0.52);
            path.lineTo(x + w * 0.44, y + h * 0.70);
            path.lineTo(x + w * 0.76, y + h * 0.32);
            p.drawPath(path);
        } else {
            p.setPen(QPen(QColor(dk ? QStringLiteral("#4B5563")
                                    : QStringLiteral("#D1D5DB")), 1.5));
            p.setBrush(QColor(dk ? QStringLiteral("#1F2124")
                                 : QStringLiteral("#FFFFFF")));
            p.drawRoundedRect(box, 5, 5);
        }

        p.setFont(labelFont());
        p.setPen(QColor(dk ? QStringLiteral("#E5E7EB") : QStringLiteral("#111827")));
        p.drawText(QRect(kBox + kSpacing, 0, width() - kBox - kSpacing, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, text());
    }

private:
    [[nodiscard]] QFont labelFont() const
    {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() : 10);
        f.setBold(true);
        return f;
    }
};
