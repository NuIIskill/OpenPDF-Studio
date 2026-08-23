#pragma once

#include "ui/theme/Theme.hpp"

#include <QFrame>
#include <QLabel>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>

class LangRow : public QFrame
{
    Q_OBJECT
public:
    explicit LangRow(const QString &code, const QString &displayName,
                     QWidget *parent = nullptr)
        : QFrame(parent), m_code(code)
    {
        setFixedHeight(48);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, true);

        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(20, 0, 20, 0);
        row->setSpacing(12);

        m_nameLabel = new QLabel(displayName, this);
        m_nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(m_nameLabel, 1);

        m_checkLabel = new QLabel(QStringLiteral("✓"), this);
        m_checkLabel->setFixedWidth(24);
        m_checkLabel->setAlignment(Qt::AlignCenter);
        m_checkLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(m_checkLabel);

        applyStyle(false);
    }

    QString code() const { return m_code; }
    bool isSelected() const { return m_selected; }

    void setSelected(bool v)
    {
        m_selected = v;
        m_checkLabel->setVisible(v);
        applyStyle(v);
    }

Q_SIGNALS:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) Q_EMIT clicked();
    }

private:
    void applyStyle(bool sel)
    {
        const bool dk = Theme::DarkMode;
        const QString selBg = dk ? "#1E3A5F" : "#EFF6FF";
        const QString hovBg = dk ? "#3E3E3E" : "#F3F4F6";
        const QString txtSel = dk ? "#93C5FD" : "#1D4ED8";
        const QString txtNor = dk ? "#D8D8D8" : "#111827";
        const QString chkClr = dk ? "#93C5FD" : "#2563EB";

        setStyleSheet(sel
            ? QStringLiteral("LangRow { background:%1; border:none; }"
                              "LangRow:hover { background:%1; }").arg(selBg)
            : QStringLiteral("LangRow { background:transparent; border:none; }"
                              "LangRow:hover { background:%1; }").arg(hovBg));

        m_nameLabel->setStyleSheet(QStringLiteral(
            "font-size:14px; color:%1; font-weight:%2;")
            .arg(sel ? txtSel : txtNor, sel ? QLatin1String("600") : QLatin1String("400")));

        m_checkLabel->setStyleSheet(QStringLiteral(
            "font-size:14px; font-weight:700; color:%1;").arg(chkClr));
    }

    QLabel *m_nameLabel  { nullptr };
    QLabel *m_checkLabel { nullptr };
    QString m_code;
    bool    m_selected   { false };
};
