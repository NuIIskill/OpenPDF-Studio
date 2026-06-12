#include "StatusBar.hpp"

#include "ui/widgets/IconButton.hpp"

#include <QLabel>
#include <QHBoxLayout>
#include <QFrame>

StatusBar::StatusBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("StatusBar"));
    setFixedHeight(44);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    buildLayout();
}

void StatusBar::setCurrentPage(int page, int total)
{
    m_pageLabel->setText(tr("Seite %1 / %2").arg(page).arg(total));
}

void StatusBar::setZoom(int percent)
{
    m_zoomLabel->setText(QStringLiteral("%1 %").arg(percent));
}

void StatusBar::buildLayout()
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(4);

    // ── Page indicator ────────────────────────────────────────────────────
    m_pageLabel = new QLabel(tr("Seite 1 / 1"), this);
    m_pageLabel->setObjectName(QStringLiteral("StatusPageLabel"));
    layout->addWidget(m_pageLabel);

    layout->addStretch(1);

    // ── Save / Print ──────────────────────────────────────────────────────
    m_saveBtn = new IconButton(QStringLiteral("💾"), this);
    m_saveBtn->setToolTip(tr("Speichern"));
    connect(m_saveBtn, &QPushButton::clicked, this, &StatusBar::saveRequested);
    layout->addWidget(m_saveBtn);

    m_printBtn = new IconButton(QStringLiteral("🖨"), this);
    m_printBtn->setToolTip(tr("Drucken"));
    connect(m_printBtn, &QPushButton::clicked, this, &StatusBar::printRequested);
    layout->addWidget(m_printBtn);

    layout->addSpacing(4);
    layout->addWidget(makeSeparator());
    layout->addSpacing(4);

    // ── Zoom controls ─────────────────────────────────────────────────────
    m_zoomOutBtn = new IconButton(QStringLiteral("−"), this);
    m_zoomOutBtn->setToolTip(tr("Verkleinern"));
    connect(m_zoomOutBtn, &QPushButton::clicked, this, &StatusBar::zoomOutRequested);
    layout->addWidget(m_zoomOutBtn);

    m_zoomLabel = new QLabel(QStringLiteral("100 %"), this);
    m_zoomLabel->setObjectName(QStringLiteral("StatusZoomLabel"));
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setMinimumWidth(48);
    layout->addWidget(m_zoomLabel);

    m_zoomInBtn = new IconButton(QStringLiteral("+"), this);
    m_zoomInBtn->setToolTip(tr("Vergrößern"));
    connect(m_zoomInBtn, &QPushButton::clicked, this, &StatusBar::zoomInRequested);
    layout->addWidget(m_zoomInBtn);

    layout->addSpacing(4);
    layout->addWidget(makeSeparator());
    layout->addSpacing(4);

    // ── Settings ──────────────────────────────────────────────────────────
    m_settingsBtn = new IconButton(QStringLiteral("⚙"), this);
    m_settingsBtn->setToolTip(tr("Einstellungen"));
    connect(m_settingsBtn, &QPushButton::clicked, this, &StatusBar::settingsRequested);
    layout->addWidget(m_settingsBtn);
}

QWidget *StatusBar::makeSeparator()
{
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Plain);
    sep->setFixedWidth(1);
    sep->setFixedHeight(20);
    sep->setStyleSheet(QStringLiteral("QFrame { background: #E2E8F0; border: none; }"));
    return sep;
}
