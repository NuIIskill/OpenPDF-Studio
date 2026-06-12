#include "TopToolbar.hpp"

#include "ui/widgets/IconButton.hpp"

#include <QLabel>
#include <QHBoxLayout>
#include <QFrame>

TopToolbar::TopToolbar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("TopToolbar"));
    setFixedHeight(56);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    buildLayout();
}

void TopToolbar::setFileName(const QString &name)
{
    m_fileNameLabel->setText(name.isEmpty() ? tr("Kein Dokument") : name);
}

void TopToolbar::setZoom(int percent)
{
    m_zoomLabel->setText(QStringLiteral("%1 %").arg(percent));
}

void TopToolbar::buildLayout()
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 0, 16, 0);
    layout->setSpacing(4);

    // ── Left: app name + file name ────────────────────────────────────────
    auto *appName = new QLabel(QStringLiteral("OpenPDF Studio"), this);
    appName->setObjectName(QStringLiteral("AppNameLabel"));
    layout->addWidget(appName);

    layout->addWidget(makeSeparator());
    layout->addSpacing(4);

    m_fileNameLabel = new QLabel(tr("Kein Dokument"), this);
    m_fileNameLabel->setObjectName(QStringLiteral("FileNameLabel"));
    layout->addWidget(m_fileNameLabel);

    layout->addStretch(1);

    // ── Right: save + print ───────────────────────────────────────────────
    m_saveBtn = new IconButton(QStringLiteral("💾"), this);
    m_saveBtn->setToolTip(tr("Speichern"));
    connect(m_saveBtn, &QPushButton::clicked, this, &TopToolbar::saveRequested);
    layout->addWidget(m_saveBtn);

    m_printBtn = new IconButton(QStringLiteral("🖨"), this);
    m_printBtn->setToolTip(tr("Drucken"));
    connect(m_printBtn, &QPushButton::clicked, this, &TopToolbar::printRequested);
    layout->addWidget(m_printBtn);

    layout->addSpacing(4);
    layout->addWidget(makeSeparator());
    layout->addSpacing(4);

    // ── Zoom controls ─────────────────────────────────────────────────────
    m_zoomOutBtn = new IconButton(QStringLiteral("−"), this);
    m_zoomOutBtn->setToolTip(tr("Verkleinern"));
    connect(m_zoomOutBtn, &QPushButton::clicked, this, &TopToolbar::zoomOutRequested);
    layout->addWidget(m_zoomOutBtn);

    m_zoomLabel = new QLabel(QStringLiteral("100 %"), this);
    m_zoomLabel->setObjectName(QStringLiteral("ZoomLabel"));
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setMinimumWidth(48);
    layout->addWidget(m_zoomLabel);

    m_zoomInBtn = new IconButton(QStringLiteral("+"), this);
    m_zoomInBtn->setToolTip(tr("Vergrößern"));
    connect(m_zoomInBtn, &QPushButton::clicked, this, &TopToolbar::zoomInRequested);
    layout->addWidget(m_zoomInBtn);
}

QWidget *TopToolbar::makeSeparator()
{
    auto *sep = new QFrame(this);
    sep->setObjectName(QStringLiteral("ToolbarSeparator"));
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Plain);
    sep->setFixedWidth(1);
    sep->setFixedHeight(24);
    sep->setStyleSheet(QStringLiteral("QFrame { background: #E2E8F0; border: none; }"));
    return sep;
}
