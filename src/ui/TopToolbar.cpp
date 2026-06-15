#include "TopToolbar.hpp"
#include "ui/widgets/IconButton.hpp"
#include "ui/theme/Theme.hpp"

#include <QLabel>
#include <QHBoxLayout>
#include <QFrame>
#include <QPushButton>

TopToolbar::TopToolbar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("TopToolbar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(56);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    buildLayout();
}

void TopToolbar::setFileName(const QString &name)
{
    m_tabLabel->setText(name.isEmpty() ? tr("Kein Dokument") : name);
}

void TopToolbar::setZoom(int percent)
{
    m_zoomLabel->setText(QStringLiteral("%1 %").arg(percent));
}

void TopToolbar::refreshTheme()
{
    const QColor nc = Theme::IconNormal;
    m_saveBtn->setIconName(m_saveBtn->iconName(), nc);
    m_printBtn->setIconName(m_printBtn->iconName(), nc);
    m_undoBtn->setIconName(m_undoBtn->iconName(), nc);
    m_redoBtn->setIconName(m_redoBtn->iconName(), nc);
    m_zoomOutBtn->setIconName(m_zoomOutBtn->iconName(), nc);
    m_zoomInBtn->setIconName(m_zoomInBtn->iconName(), nc);
    m_viewSingleBtn->setIconName(m_viewSingleBtn->iconName(), nc);
    m_viewGridBtn->setIconName(m_viewGridBtn->iconName(), nc);
}

void TopToolbar::buildLayout()
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(2);

    // ── Logo ──────────────────────────────────────────────────────────────
    auto *logo = new QLabel(QStringLiteral("O"), this);
    logo->setObjectName(QStringLiteral("AppLogo"));
    logo->setFixedSize(32, 32);
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);
    layout->addSpacing(6);

    auto *appName = new QLabel(QStringLiteral("OpenPDF Studio"), this);
    appName->setObjectName(QStringLiteral("AppNameLabel"));
    layout->addWidget(appName);

    layout->addSpacing(4);
    layout->addWidget(makeSeparator());
    layout->addSpacing(8);

    // ── Document tab ──────────────────────────────────────────────────────
    auto *tab = new QFrame(this);
    tab->setObjectName(QStringLiteral("DocTab"));
    tab->setFixedHeight(34);
    auto *tabLayout = new QHBoxLayout(tab);
    tabLayout->setContentsMargins(10, 0, 6, 0);
    tabLayout->setSpacing(6);

    m_tabLabel = new QLabel(tr("Kein Dokument"), tab);
    m_tabLabel->setObjectName(QStringLiteral("DocTabLabel"));
    tabLayout->addWidget(m_tabLabel);

    auto *closeBtn = new IconButton(tab);
    closeBtn->setIconName(QStringLiteral("x"), QColor("#9CA3AF"));
    closeBtn->setFixedSize(22, 22);
    closeBtn->setIconSize(QSize(14, 14));
    tabLayout->addWidget(closeBtn);

    layout->addWidget(tab);
    layout->addSpacing(4);

    // ── New tab button ────────────────────────────────────────────────────
    auto *newTabBtn = new IconButton(this);
    newTabBtn->setObjectName(QStringLiteral("NewTabBtn"));
    newTabBtn->setIconName(QStringLiteral("plus"), QColor("#9CA3AF"));
    newTabBtn->setFixedSize(28, 28);
    newTabBtn->setIconSize(QSize(16, 16));
    layout->addWidget(newTabBtn);

    layout->addStretch(1);

    // ── Save / Print ──────────────────────────────────────────────────────
    m_saveBtn = new IconButton(this);
    m_saveBtn->setIconName(QStringLiteral("save"));
    m_saveBtn->setToolTip(tr("Speichern"));
    connect(m_saveBtn, &QPushButton::clicked, this, &TopToolbar::saveRequested);
    layout->addWidget(m_saveBtn);

    m_printBtn = new IconButton(this);
    m_printBtn->setIconName(QStringLiteral("printer"));
    m_printBtn->setToolTip(tr("Drucken"));
    connect(m_printBtn, &QPushButton::clicked, this, &TopToolbar::printRequested);
    layout->addWidget(m_printBtn);

    layout->addSpacing(2);
    layout->addWidget(makeSeparator());
    layout->addSpacing(2);

    // ── Undo / Redo ───────────────────────────────────────────────────────
    m_undoBtn = new IconButton(this);
    m_undoBtn->setIconName(QStringLiteral("undo-2"));
    m_undoBtn->setToolTip(tr("Rückgängig"));
    connect(m_undoBtn, &QPushButton::clicked, this, &TopToolbar::undoRequested);
    layout->addWidget(m_undoBtn);

    m_redoBtn = new IconButton(this);
    m_redoBtn->setIconName(QStringLiteral("redo-2"));
    m_redoBtn->setToolTip(tr("Wiederherstellen"));
    connect(m_redoBtn, &QPushButton::clicked, this, &TopToolbar::redoRequested);
    layout->addWidget(m_redoBtn);

    layout->addSpacing(2);
    layout->addWidget(makeSeparator());
    layout->addSpacing(2);

    // ── Zoom ──────────────────────────────────────────────────────────────
    m_zoomOutBtn = new IconButton(this);
    m_zoomOutBtn->setIconName(QStringLiteral("zoom-out"));
    m_zoomOutBtn->setToolTip(tr("Verkleinern"));
    connect(m_zoomOutBtn, &QPushButton::clicked, this, &TopToolbar::zoomOutRequested);
    layout->addWidget(m_zoomOutBtn);

    m_zoomLabel = new QLabel(QStringLiteral("100 %"), this);
    m_zoomLabel->setObjectName(QStringLiteral("ZoomLabel"));
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setMinimumWidth(52);
    layout->addWidget(m_zoomLabel);

    m_zoomInBtn = new IconButton(this);
    m_zoomInBtn->setIconName(QStringLiteral("zoom-in"));
    m_zoomInBtn->setToolTip(tr("Vergrößern"));
    connect(m_zoomInBtn, &QPushButton::clicked, this, &TopToolbar::zoomInRequested);
    layout->addWidget(m_zoomInBtn);

    layout->addSpacing(2);
    layout->addWidget(makeSeparator());
    layout->addSpacing(2);

    // ── View toggles ──────────────────────────────────────────────────────
    m_viewSingleBtn = new IconButton(this);
    m_viewSingleBtn->setIconName(QStringLiteral("square"));
    m_viewSingleBtn->setToolTip(tr("Einzelseite"));
    m_viewSingleBtn->setCheckable(true);
    m_viewSingleBtn->setChecked(true);
    layout->addWidget(m_viewSingleBtn);

    m_viewGridBtn = new IconButton(this);
    m_viewGridBtn->setIconName(QStringLiteral("layout-grid"));
    m_viewGridBtn->setToolTip(tr("Rasteransicht"));
    m_viewGridBtn->setCheckable(true);
    layout->addWidget(m_viewGridBtn);
}

QWidget *TopToolbar::makeSeparator()
{
    auto *sep = new QFrame(this);
    sep->setObjectName(QStringLiteral("ToolbarSeparator"));
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Plain);
    sep->setFixedWidth(1);
    sep->setFixedHeight(22);
    const QString sepColor = Theme::DarkMode ? QStringLiteral("#3A3A3A") : QStringLiteral("#E5E7EB");
    sep->setStyleSheet(QStringLiteral("QFrame { background: %1; border: none; }").arg(sepColor));
    return sep;
}
