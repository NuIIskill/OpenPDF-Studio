#pragma once

#include "ui/theme/Theme.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPoint>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

/// Row geometry of the history timeline.
namespace HistoryConst {
    constexpr int kRowHeight  = 74;
    constexpr int kMarkerCol  = 56;
    constexpr int kMarkerSize = 30;
}

class HistoryRow : public QFrame
{
    Q_OBJECT

public:
    HistoryRow(int index, int number, const QString &time, const QString &title,
               const QString &detail, const QString &iconName,
               bool first, bool last, QWidget *parent = nullptr)
        : QFrame(parent)
        , m_index(index), m_first(first), m_last(last)
    {
        setObjectName(QStringLiteral("HistoryRow"));
        setAttribute(Qt::WA_StyledBackground, true);
        setFixedHeight(HistoryConst::kRowHeight);
        setCursor(Qt::PointingHandCursor);

        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(0, 0, 12, 0);
        h->setSpacing(0);

        m_marker = new QLabel(QString::number(number), this);
        m_marker->setObjectName(QStringLiteral("HistoryMarker"));
        m_marker->setAlignment(Qt::AlignCenter);
        m_marker->setFixedSize(HistoryConst::kMarkerSize, HistoryConst::kMarkerSize);

        auto *markerCol = new QWidget(this);
        markerCol->setFixedWidth(HistoryConst::kMarkerCol);
        auto *mv = new QHBoxLayout(markerCol);
        mv->setContentsMargins(0, 0, 0, 0);
        mv->addWidget(m_marker, 0, Qt::AlignCenter);
        h->addWidget(markerCol);

        m_time = new QLabel(time, this);
        m_time->setObjectName(QStringLiteral("HistoryTime"));
        m_time->setFixedWidth(46);
        h->addWidget(m_time);
        h->addSpacing(12);

        auto *textCol = new QVBoxLayout;
        textCol->setContentsMargins(0, 0, 0, 0);
        textCol->setSpacing(2);
        m_title = new QLabel(title, this);
        m_title->setObjectName(QStringLiteral("HistoryTitle"));
        textCol->addWidget(m_title);
        m_detail = new QLabel(detail, this);
        m_detail->setObjectName(QStringLiteral("HistoryDetail"));
        m_detail->setVisible(!detail.isEmpty());
        textCol->addWidget(m_detail);
        h->addLayout(textCol, 1);

        m_kindIcon = new QLabel(this);
        m_kindIcon->setFixedSize(24, 24);
        m_kindIcon->setAlignment(Qt::AlignCenter);
        m_iconName = iconName;
        h->addWidget(m_kindIcon);
        h->addSpacing(6);

        m_menuBtn = new QToolButton(this);
        m_menuBtn->setObjectName(QStringLiteral("HistoryMenuBtn"));
        m_menuBtn->setFixedSize(28, 28);
        m_menuBtn->setCursor(Qt::PointingHandCursor);
        m_menuBtn->setAutoRaise(true);
        connect(m_menuBtn, &QToolButton::clicked, this, [this]() {
            Q_EMIT menuRequested(m_index,
                                 m_menuBtn->mapToGlobal(QPoint(0, m_menuBtn->height())));
        });
        h->addWidget(m_menuBtn);

        refreshIcons();
    }

    int  index() const { return m_index; }
    bool isSelected() const { return m_selected; }

    void setSelected(bool on)
    {
        if (m_selected == on) return;
        m_selected = on;
        setProperty("selected", on);
        m_marker->setProperty("selected", on);
        for (QWidget *w : { static_cast<QWidget *>(this),
                            static_cast<QWidget *>(m_marker) }) {
            w->style()->unpolish(w);
            w->style()->polish(w);
        }
        refreshIcons();
    }

    void refreshIcons()
    {
        const QColor col = m_selected ? Theme::IconChecked : Theme::IconMuted;
        const QPixmap px = Theme::renderSvg(m_iconName, col, 20);
        if (!px.isNull()) m_kindIcon->setPixmap(px);
        const QPixmap dots = Theme::renderSvg(QStringLiteral("more-horizontal"),
                                              Theme::IconMuted, 18);
        if (!dots.isNull()) m_menuBtn->setIcon(QIcon(dots));
    }

Q_SIGNALS:
    void clicked(int index);
    void doubleClicked(int index);
    void menuRequested(int index, const QPoint &globalPos);

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) Q_EMIT clicked(m_index);
        QFrame::mousePressEvent(e);
    }

    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) Q_EMIT doubleClicked(m_index);
        QFrame::mouseDoubleClickEvent(e);
    }

    void paintEvent(QPaintEvent *e) override
    {
        QFrame::paintEvent(e);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(Theme::DarkMode ? QColor(0x4A, 0x4A, 0x4A)
                                      : QColor(0xE5, 0xE7, 0xEB), 2));
        const int cx  = HistoryConst::kMarkerCol / 2;
        const int top = height() / 2 - HistoryConst::kMarkerSize / 2;
        const int bot = height() / 2 + HistoryConst::kMarkerSize / 2;
        if (!m_first) p.drawLine(cx, 0, cx, top);
        if (!m_last)  p.drawLine(cx, bot, cx, height());
    }

private:
    int      m_index;
    bool     m_first;
    bool     m_last;
    bool     m_selected { false };
    QString  m_iconName;
    QLabel  *m_marker   { nullptr };
    QLabel  *m_time     { nullptr };
    QLabel  *m_title    { nullptr };
    QLabel  *m_detail   { nullptr };
    QLabel  *m_kindIcon { nullptr };
    QToolButton *m_menuBtn { nullptr };
};
