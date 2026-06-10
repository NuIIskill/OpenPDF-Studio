#include "StatusBar.hpp"

#include <QLabel>
#include <QHBoxLayout>

StatusBar::StatusBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("StatusBar"));
    setFixedHeight(28);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(16);

    // ── Page indicator ──────────────────────────────────────────────────
    m_pageLabel = new QLabel(tr("Seite 1 / 3"), this);
    m_pageLabel->setObjectName(QStringLiteral("StatusPageLabel"));
    layout->addWidget(m_pageLabel);

    layout->addStretch(1);

    // ── Zoom level ──────────────────────────────────────────────────────
    m_zoomLabel = new QLabel(QStringLiteral("100 %"), this);
    m_zoomLabel->setObjectName(QStringLiteral("StatusZoomLabel"));
    layout->addWidget(m_zoomLabel);

    // ── File size ───────────────────────────────────────────────────────
    m_fileSizeLabel = new QLabel(QStringLiteral("—"), this);
    m_fileSizeLabel->setObjectName(QStringLiteral("StatusFileSizeLabel"));
    layout->addWidget(m_fileSizeLabel);
}

void StatusBar::setCurrentPage(int page, int total)
{
    m_pageLabel->setText(tr("Seite %1 / %2").arg(page).arg(total));
}

void StatusBar::setZoom(int percent)
{
    m_zoomLabel->setText(QStringLiteral("%1 %").arg(percent));
}

void StatusBar::setFileSize(const QString &text)
{
    m_fileSizeLabel->setText(text.isEmpty() ? QStringLiteral("—") : text);
}
