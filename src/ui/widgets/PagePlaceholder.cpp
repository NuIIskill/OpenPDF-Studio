#include "PagePlaceholder.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QFont>

PagePlaceholder::PagePlaceholder(int pageNumber, QWidget *parent)
    : QWidget(parent)
    , m_pageNumber(pageNumber)
{
    setObjectName(QStringLiteral("PagePlaceholder"));
    // A4 at 72 dpi: 595 × 842
    setFixedSize(595, 842);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void PagePlaceholder::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r(0, 0, width(), height());

    // ── Drop shadow (multi-layer approximation) ────────────────────────
    for (int i = 4; i >= 1; --i) {
        const qreal alpha = 0.04 * (5 - i);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, static_cast<int>(alpha * 255)));
        p.drawRoundedRect(r.adjusted(i, i, i, i), 4, 4);
    }

    // ── Page surface ───────────────────────────────────────────────────
    p.setBrush(Qt::white);
    p.setPen(QPen(QColor("#E2E8F0"), 1.0));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);

    // ── Header bar mock ────────────────────────────────────────────────
    // Simulates a document title area at the top
    const int margin = 40;
    const int contentW = width() - margin * 2;
    int y = 56;

    // Title line
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#CBD5E1"));
    p.drawRoundedRect(margin, y, static_cast<int>(contentW * 0.55), 10, 3, 3);
    y += 20;

    // Sub-title line
    p.setBrush(QColor("#E2E8F0"));
    p.drawRoundedRect(margin, y, static_cast<int>(contentW * 0.35), 7, 2, 2);
    y += 32;

    // ── Horizontal rule ────────────────────────────────────────────────
    p.setPen(QPen(QColor("#E2E8F0"), 1));
    p.drawLine(margin, y, width() - margin, y);
    y += 20;
    p.setPen(Qt::NoPen);

    // ── Body text lines ────────────────────────────────────────────────
    const QList<qreal> widths = {
        1.0, 0.92, 0.88, 1.0, 0.60,   // paragraph 1
        0.0,                            // blank spacer
        1.0, 0.95, 0.78, 1.0, 0.84, 0.45  // paragraph 2
    };

    for (qreal wf : widths) {
        if (wf == 0.0) {
            y += 16; // spacer
            continue;
        }
        p.setBrush(QColor("#E2E8F0"));
        p.drawRoundedRect(margin, y,
                          static_cast<int>(contentW * wf), 7,
                          2, 2);
        y += 14;
    }

    y += 24;

    // ── Image / block placeholder ──────────────────────────────────────
    const int blockH = 120;
    p.setBrush(QColor("#F1F5F9"));
    p.setPen(QPen(QColor("#CBD5E1"), 1));
    p.drawRoundedRect(margin, y, contentW, blockH, 6, 6);

    // Landscape icon hint inside block
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#CBD5E1"));
    const int iconSize = 32;
    const int iconX = margin + (contentW - iconSize) / 2;
    const int iconY = y + (blockH - iconSize) / 2;
    p.drawRoundedRect(iconX, iconY, iconSize, iconSize, 8, 8);

    y += blockH + 24;

    // ── Second body block ──────────────────────────────────────────────
    p.setPen(Qt::NoPen);
    const QList<qreal> widths2 = { 1.0, 0.90, 0.82, 1.0, 0.68 };
    for (qreal wf : widths2) {
        p.setBrush(QColor("#E2E8F0"));
        p.drawRoundedRect(margin, y,
                          static_cast<int>(contentW * wf), 7,
                          2, 2);
        y += 14;
    }

    // ── Footer / page number ───────────────────────────────────────────
    p.setPen(QColor("#94A3B8"));
    QFont f;
    f.setPixelSize(11);
    p.setFont(f);
    const QRect footer(0, height() - 36, width(), 24);
    p.drawText(footer, Qt::AlignHCenter | Qt::AlignVCenter,
               QString::number(m_pageNumber));
}
