#pragma once

#include "ui/theme/Theme.hpp"

#include <QAbstractButton>
#include <QPainter>
#include <QPainterPath>

/// Provides a themed checkbox for application settings.
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

    static constexpr int kBox     = 20;
    static constexpr int kSpacing = 14;

    void setTristate(bool on) { m_tristate = on; }

    [[nodiscard]] Qt::CheckState checkState() const
    {
        return m_partial ? Qt::PartiallyChecked : isChecked() ? Qt::Checked : Qt::Unchecked;
    }

    void setCheckState(Qt::CheckState state)
    {
        m_partial = state == Qt::PartiallyChecked;
        setChecked(state == Qt::Checked);
        update();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const QFontMetrics fm(labelFont());
        return { kBox + kSpacing + fm.horizontalAdvance(text()),
                 qMax(kBox, fm.height()) };
    }

protected:
    void nextCheckState() override
    {
        if (!m_tristate) {
            QAbstractButton::nextCheckState();
            return;
        }
        setCheckState(m_partial ? Qt::Checked : isChecked() ? Qt::Unchecked : Qt::PartiallyChecked);
    }

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
        } else if (m_partial) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#EAB308")));
            p.drawRoundedRect(box, 5, 5);
            p.setPen(QPen(Qt::white, 2.4, Qt::SolidLine, Qt::RoundCap));
            const qreal y = box.center().y();
            p.drawLine(QPointF(box.left() + box.width() * 0.28, y),
                       QPointF(box.right() - box.width() * 0.28, y));
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
    bool m_tristate { false };
    bool m_partial  { false };

    [[nodiscard]] QFont labelFont() const
    {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() : 10);
        f.setBold(true);
        return f;
    }
};
