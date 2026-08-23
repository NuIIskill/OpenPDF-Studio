#pragma once

#include "ui/theme/Theme.hpp"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QPainter>

class OptionCard : public QFrame
{
    Q_OBJECT
public:
    explicit OptionCard(const QString &iconName, const QString &title,
                        const QString &desc, QWidget *parent = nullptr)
        : QFrame(parent), m_iconName(iconName)
    {
        setFixedSize(175, 112);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, true);

        auto *vbox = new QVBoxLayout(this);
        vbox->setContentsMargins(12, 12, 12, 10);
        vbox->setSpacing(5);
        vbox->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

        m_iconLabel = new QLabel(this);
        m_iconLabel->setFixedSize(26, 26);
        m_iconLabel->setAlignment(Qt::AlignCenter);
        m_iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        vbox->addWidget(m_iconLabel, 0, Qt::AlignHCenter);

        m_titleLabel = new QLabel(title, this);
        m_titleLabel->setAlignment(Qt::AlignCenter);
        m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        vbox->addWidget(m_titleLabel);

        m_descLabel = new QLabel(desc, this);
        m_descLabel->setAlignment(Qt::AlignCenter);
        m_descLabel->setWordWrap(true);
        m_descLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        vbox->addWidget(m_descLabel);

        m_check = new QLabel(QStringLiteral("✓"), this);
        m_check->setFixedSize(20, 20);
        m_check->setAlignment(Qt::AlignCenter);
        m_check->hide();

        applyStyle(false);
    }

    bool isSelected() const { return m_selected; }
    void setSelected(bool v) { m_selected = v; m_check->setVisible(v); applyStyle(v); }
    const QString &iconName() const { return m_iconName; }

Q_SIGNALS:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) Q_EMIT clicked();
    }
    void resizeEvent(QResizeEvent *e) override
    {
        QFrame::resizeEvent(e);
        m_check->move(width() - 26, 5);
    }

private:
    void applyStyle(bool sel)
    {
        const bool dk = Theme::DarkMode;
        const QColor iconColor = sel ? QColor("#3B82F6")
                                     : QColor(dk ? "#9CA3AF" : "#6B7280");
        const QPixmap px = Theme::renderSvg(m_iconName, iconColor, 22);
        if (!px.isNull()) m_iconLabel->setPixmap(px);

        if (sel) {
            const QString bg = dk ? "#1E3358" : "#FFFFFF";
            setStyleSheet(QStringLiteral(
                "OptionCard { border:2px solid #3B82F6; border-radius:8px; background:%1; }").arg(bg));
            m_check->setStyleSheet(QStringLiteral(
                "background:#3B82F6; border-radius:10px; color:white; font-size:12px; font-weight:700;"));
            m_titleLabel->setStyleSheet(dk
                ? QStringLiteral("font-size:12px; font-weight:600; color:#93C5FD;")
                : QStringLiteral("font-size:12px; font-weight:600; color:#1D4ED8;"));
            m_descLabel->setStyleSheet(dk
                ? QStringLiteral("font-size:10px; color:#60A5FA;")
                : QStringLiteral("font-size:10px; color:#3B82F6;"));
        } else {
            const QString bg  = dk ? "#404040" : "#FFFFFF";
            const QString bdr = dk ? "#555555" : "#E5E7EB";
            setStyleSheet(QStringLiteral(
                "OptionCard { border:1px solid %1; border-radius:8px; background:%2; }").arg(bdr, bg));
            m_titleLabel->setStyleSheet(dk
                ? QStringLiteral("font-size:12px; font-weight:600; color:#D0D0D0;")
                : QStringLiteral("font-size:12px; font-weight:600; color:#111827;"));
            m_descLabel->setStyleSheet(dk
                ? QStringLiteral("font-size:10px; color:#9CA3AF;")
                : QStringLiteral("font-size:10px; color:#6B7280;"));
        }
    }

    QLabel *m_iconLabel  { nullptr };
    QLabel *m_titleLabel { nullptr };
    QLabel *m_descLabel  { nullptr };
    QLabel *m_check      { nullptr };
    QString m_iconName;
    bool    m_selected   { false };
};
