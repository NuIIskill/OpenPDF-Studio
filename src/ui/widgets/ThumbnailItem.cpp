#include "ui/widgets/ThumbnailItem.hpp"

#include "ui/theme/Theme.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QStyle>

ThumbnailItem::ThumbnailItem(int pageNumber, QWidget *parent)
    : QWidget(parent)
    , m_pageNumber(pageNumber)
{
    setObjectName(QStringLiteral("ThumbnailItem"));
    setFixedHeight(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void ThumbnailItem::setSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    // Propagate to QSS dynamic property
    setProperty("selected", selected);
    style()->unpolish(this);
    style()->polish(this);
    update();
    Q_EMIT selectionChanged(selected);
}

void ThumbnailItem::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect r = rect();

    // ── Background ────────────────────────────────────────────────────────
    QColor bg = QColor("#1E293B");
    if (m_selected)
        bg = QColor("#1D4ED8");
    else if (m_hovered)
        bg = QColor("#334155");

    p.fillRect(r, bg);

    // ── Selected left-edge indicator ──────────────────────────────────────
    if (m_selected) {
        p.fillRect(QRect(0, 0, 3, r.height()), QColor("#60A5FA"));
    }

    // ── Miniature page preview ────────────────────────────────────────────
    // A4 ratio: 595:842 ≈ 0.707.  We target ~100 px wide inside the 220 px
    // sidebar, centred, with some top margin.
    constexpr int previewW = 96;
    constexpr int previewH = static_cast<int>(previewW / 0.707);
    const int previewX = (r.width() - previewW) / 2;
    const int previewY = 14;

    QRectF pageRect(previewX, previewY, previewW, previewH);

    // Drop shadow (cheap approximation)
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 32));
    p.drawRoundedRect(pageRect.adjusted(2, 2, 2, 2), 3, 3);

    // Page surface
    p.setBrush(Qt::white);
    p.setPen(QPen(QColor("#E2E8F0"), 0.5));
    p.drawRoundedRect(pageRect, 3, 3);

    // Mock content lines
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#E2E8F0"));
    const int lx = previewX + 8;
    const int lw = previewW - 16;
    int ly = previewY + 10;
    // Title bar mock
    p.setBrush(QColor("#CBD5E1"));
    p.drawRoundedRect(lx, ly, lw, 4, 2, 2);
    ly += 10;
    // Content lines mock
    p.setBrush(QColor("#E2E8F0"));
    for (int i = 0; i < 5; ++i) {
        const int lineW = (i % 2 == 0) ? lw : lw - 12;
        p.drawRoundedRect(lx, ly, lineW, 3, 1, 1);
        ly += 7;
    }

    // ── Page number label ─────────────────────────────────────────────────
    p.setPen(m_selected ? QColor("#BFDBFE") : QColor("#94A3B8"));
    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    const QRect labelRect(0, previewY + previewH + 8, r.width(), 16);
    p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop,
               QString::number(m_pageNumber));
}

void ThumbnailItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        Q_EMIT clicked(m_pageNumber);
    }
    QWidget::mousePressEvent(event);
}

void ThumbnailItem::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void ThumbnailItem::leaveEvent(QEvent *event)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}
