#include "ui/sign/SignTabBar.hpp"
#include "ui/theme/Theme.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

constexpr int   kHeight    = 56;
constexpr int   kIconSize  = 24;
constexpr int   kGap       = 10;
constexpr int   kUnderline = 3;
constexpr qreal kRadius    = 10.0;

}

SignTabBar::SignTabBar(QWidget *parent)
    : QTabBar(parent)
{
    setExpanding(true);
    setDrawBase(false);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

void SignTabBar::addIconTab(const QString &icon, const QString &text)
{
    setTabData(addTab(text), icon);
}

QSize SignTabBar::tabSizeHint(int index) const
{
    return { QTabBar::tabSizeHint(index).width(), kHeight };
}

void SignTabBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()).adjusted(0, 0, 0, kRadius), kRadius, kRadius);
    p.setClipPath(clip);

    const QColor accent = Theme::Primary;
    const QColor line   = palette().color(QPalette::Mid);
    QColor tint = accent;
    tint.setAlphaF(0.12);

    for (int i = 0; i < count(); ++i) {
        const QRect r = tabRect(i);
        const bool selected = i == currentIndex();
        const bool enabled  = isTabEnabled(i);

        p.fillRect(r, palette().color(QPalette::Base));
        p.fillRect(r, selected ? tint
                               : palette().color(i == m_hovered && enabled ? QPalette::AlternateBase
                                                                           : QPalette::Window));
        if (selected)
            p.fillRect(QRect(r.left(), r.bottom() - kUnderline + 1, r.width(), kUnderline), accent);
        else
            p.fillRect(QRect(r.left(), r.bottom(), r.width(), 1), line);
        if (i > 0)
            p.fillRect(QRect(r.left(), r.top(), 1, r.height() - kUnderline), line);

        QFont f = font();
        f.setPixelSize(17);
        f.setWeight(selected ? QFont::DemiBold : QFont::Medium);
        p.setFont(f);
        const QColor fg = selected ? accent
                        : palette().color(enabled ? QPalette::Text : QPalette::PlaceholderText);
        const int textW = QFontMetrics(f).horizontalAdvance(tabText(i));
        const int left  = r.center().x() - (kIconSize + kGap + textW) / 2;
        const int midY  = r.center().y() - kUnderline / 2;

        p.drawPixmap(left, midY - kIconSize / 2,
                     Theme::renderSvg(tabData(i).toString(),
                                      selected ? accent
                                               : enabled ? Theme::IconNormal : Theme::IconDisabled,
                                      kIconSize,
                                      devicePixelRatioF()));
        p.setPen(fg);
        p.drawText(QRect(left + kIconSize + kGap, r.top(), textW + 2, r.height() - kUnderline),
                   Qt::AlignLeft | Qt::AlignVCenter, tabText(i));
    }
}

void SignTabBar::mouseMoveEvent(QMouseEvent *event)
{
    const int hovered = tabAt(event->position().toPoint());
    if (hovered != m_hovered) {
        m_hovered = hovered;
        setCursor(hovered >= 0 && isTabEnabled(hovered) ? Qt::PointingHandCursor
                                                         : Qt::ArrowCursor);
        update();
    }
    QTabBar::mouseMoveEvent(event);
}

void SignTabBar::leaveEvent(QEvent *event)
{
    m_hovered = -1;
    update();
    QTabBar::leaveEvent(event);
}
