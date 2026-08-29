#pragma once

#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

/// Card and grid geometry of the page organizer.
namespace OrgConst {
    constexpr int CARD_W    = 220;
    constexpr int CARD_H    = 265;
    constexpr int THUMB_W   = 172;
    constexpr int THUMB_H   = 210;
    constexpr int RENDER_DPI = 96;
    constexpr int COL_GAP   = 16;
    constexpr int ROW_GAP   = 16;
    constexpr int GRID_PAD  = 20;
}

/// 2-column × 3-row dot-grid grip icon, painted directly.
class DragDots : public QWidget
{
public:
    explicit DragDots(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(16, 22);
        setCursor(Qt::SizeAllCursor);
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Theme::DarkMode ? QColor(0x6A, 0x6A, 0x6A)
                                   : QColor(0xC4, 0xC9, 0xD4));
        p.setPen(Qt::NoPen);
        constexpr int R    = 2;
        constexpr int xGap = 6;
        constexpr int yGap = 6;
        const int startX = (width()  - xGap) / 2;
        const int startY = (height() - yGap * 2) / 2;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
                p.drawEllipse(QPoint(startX + col * xGap, startY + row * yGap), R, R);
    }
};

/// Displays one page tile in the organizer grid.
class PageCard : public QFrame
{
    Q_OBJECT
public:
    explicit PageCard(int index, QWidget *parent = nullptr)
        : QFrame(parent), m_index(index)
    {
        setObjectName(QStringLiteral("PageCard"));
        setFixedSize(OrgConst::CARD_W, OrgConst::CARD_H);
        setCursor(Qt::ArrowCursor);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *header = new QWidget(this);
        header->setFixedHeight(28);
        auto *hdr = new QHBoxLayout(header);
        hdr->setContentsMargins(8, 4, 8, 0);
        hdr->setSpacing(0);

        m_dragHandle = new DragDots(header);
        hdr->addWidget(m_dragHandle, 1, Qt::AlignLeft | Qt::AlignVCenter);

        m_check = new QPushButton(QStringLiteral("✓"), header);
        m_check->setObjectName(QStringLiteral("CardCheck"));
        m_check->setCheckable(true);
        m_check->setFixedSize(22, 22);
        m_check->setCursor(Qt::PointingHandCursor);
        m_check->setFocusPolicy(Qt::NoFocus);
        hdr->addWidget(m_check, 0, Qt::AlignRight | Qt::AlignVCenter);

        root->addWidget(header);

        m_thumbLabel = new QLabel(this);
        m_thumbLabel->setObjectName(QStringLiteral("ThumbLabel"));
        m_thumbLabel->setFixedSize(OrgConst::THUMB_W, OrgConst::THUMB_H);
        m_thumbLabel->setAlignment(Qt::AlignCenter);
        root->addWidget(m_thumbLabel, 0, Qt::AlignHCenter);

        m_pageLabel = new QLabel(this);
        m_pageLabel->setObjectName(QStringLiteral("PageCardLabel"));
        m_pageLabel->setAlignment(Qt::AlignCenter);
        root->addWidget(m_pageLabel, 1, Qt::AlignHCenter | Qt::AlignBottom);

        applyStyle(false);

        connect(m_check, &QPushButton::toggled, this, &PageCard::checkToggled);
    }

    void setThumb(const QPixmap &px)  { m_thumbLabel->setPixmap(px); }
    void setPageLabel(const QString &t) { m_pageLabel->setText(t); }
    void setSelected(bool s)
    {
        if (m_selected == s) return;
        m_selected = s;
        applyStyle(s);
        QSignalBlocker blk(m_check);
        m_check->setChecked(s);
    }
    bool isSelected() const { return m_selected; }
    int  index() const      { return m_index; }
    void setIndex(int i)    { m_index = i; }

Q_SIGNALS:
    void clicked(int index, Qt::KeyboardModifiers mods);
    void checkToggled(bool checked);
    void dragStarted(int index);

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_dragStart = e->pos();
            m_dragArmed = true;
            Q_EMIT clicked(m_index, e->modifiers());
        }
        QFrame::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_dragArmed && (e->buttons() & Qt::LeftButton) &&
            (e->pos() - m_dragStart).manhattanLength() > QApplication::startDragDistance())
        {

            m_dragArmed = false;
            Q_EMIT dragStarted(m_index);
            return;
        }
        QFrame::mouseMoveEvent(e);
    }

private:

    static QString childStyles()
    {
        const bool dk = Theme::DarkMode;
        return QStringLiteral(
            "QPushButton#CardCheck {"
            "  background:%1; border:2px solid %2; border-radius:5px;"
            "  color:transparent; font-size:13px; font-weight:700; padding:0;"
            "}"
            "QPushButton#CardCheck:checked {"
            "  background:#2563EB; border-color:#2563EB; color:white;"
            "}"
            "QPushButton#CardCheck:hover { border-color:%3; }"
            "QLabel#PageCardLabel { font-size:13px; font-weight:600; color:%4; padding-bottom:6px; }")
            .arg(dk ? QLatin1String("#2B2B2B") : QLatin1String("#FFFFFF"),
                 dk ? QLatin1String("#555555") : QLatin1String("#D1D5DB"),
                 dk ? QLatin1String("#3B82F6") : QLatin1String("#93C5FD"),
                 dk ? QLatin1String("#D8D8D8") : QLatin1String("#111827"));
    }

    void applyStyle(bool selected)
    {
        const bool dk = Theme::DarkMode;
        const QString bg     = dk ? QStringLiteral("#3A3A3A") : QStringLiteral("#FFFFFF");
        const QString border = dk ? QStringLiteral("#484848") : QStringLiteral("#E5E7EB");
        const QString hover  = dk ? QStringLiteral("#606060") : QStringLiteral("#CBD5E1");
        const QString sel    = dk ? QStringLiteral("#3B82F6") : QStringLiteral("#2563EB");

        if (selected) {
            setStyleSheet(QStringLiteral(
                "QFrame#PageCard { background:%1; border:2px solid %2; border-radius:10px; }")
                .arg(bg, sel)
                + childStyles());
        } else {
            setStyleSheet(QStringLiteral(
                "QFrame#PageCard { background:%1; border:1px solid %2; border-radius:10px; }"
                "QFrame#PageCard:hover { border:1px solid %3; }")
                .arg(bg, border, hover)
                + childStyles());
        }
    }

    int        m_index;
    bool       m_selected { false };
    bool       m_dragArmed { false };
    QPoint     m_dragStart;
    DragDots  *m_dragHandle  { nullptr };
    QLabel    *m_thumbLabel  { nullptr };
    QLabel    *m_pageLabel   { nullptr };
    QPushButton *m_check     { nullptr };
};
