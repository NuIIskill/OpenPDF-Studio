#include "TopToolbar.hpp"
#include "ui/widgets/IconButton.hpp"
#include "ui/theme/Theme.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>

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
    if (m_currentTab >= 0 && m_currentTab < m_tabBtns.size())
        setTabLabel(m_currentTab, name);
}

void TopToolbar::setZoom(int percent)
{
    m_zoomLabel->setText(QStringLiteral("%1 %").arg(percent));
}

void TopToolbar::setViewMode(bool gridView)
{
    m_viewSingleBtn->setChecked(!gridView);
    m_viewGridBtn->setChecked(gridView);
}

void TopToolbar::refreshTheme()
{
    const QColor nc = Theme::IconNormal;
    for (IconButton *btn : { m_saveBtn, m_printBtn, m_undoBtn, m_redoBtn,
                              m_zoomOutBtn, m_zoomInBtn, m_viewSingleBtn, m_viewGridBtn })
        btn->setIconName(btn->iconName(), nc);
}

void TopToolbar::retranslateUi()
{
    m_saveBtn->setToolTip(tr("Save"));
    m_printBtn->setToolTip(tr("Print"));
    m_undoBtn->setToolTip(tr("Undo"));
    m_redoBtn->setToolTip(tr("Redo"));
    m_zoomOutBtn->setToolTip(tr("Zoom Out"));
    m_zoomInBtn->setToolTip(tr("Zoom In"));
    m_viewSingleBtn->setToolTip(tr("Single Page"));
    m_viewGridBtn->setToolTip(tr("Grid View"));

    for (int i = 0; i < m_tabBtns.size(); ++i) {
        if (m_tabEmpty[i]) {
            const QString text = tr("No Document");
            m_tabLabels[i]->setText(text);
            m_tabBtns[i]->setFixedWidth(
                qMax(100, m_tabBtns[i]->fontMetrics().horizontalAdvance(text) + 46));
        }
    }
    syncTabBarWidth();
}

// ── Tab management ────────────────────────────────────────────────────────────

int TopToolbar::addTab(const QString &label)
{
    const int idx    = m_tabBtns.size();
    const bool empty = label.isEmpty();

    auto *btn = new QPushButton(m_tabBar);
    btn->setObjectName(QStringLiteral("DocTab"));
    btn->setFixedHeight(34);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setCheckable(true);

    auto *inner = new QHBoxLayout(btn);
    inner->setContentsMargins(10, 0, 4, 0);
    inner->setSpacing(4);

    const QString tabText = empty ? tr("No Document") : label;
    auto *lbl = new QLabel(tabText, btn);
    lbl->setObjectName(QStringLiteral("DocTabLabel"));
    lbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    lbl->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    inner->addWidget(lbl);

    auto *closeBtn = new IconButton(btn);
    closeBtn->setIconName(QStringLiteral("x"), QColor("#9CA3AF"));
    closeBtn->setFixedSize(20, 20);
    closeBtn->setIconSize(QSize(12, 12));
    closeBtn->setFocusPolicy(Qt::NoFocus);
    inner->addWidget(closeBtn);

    btn->setFixedWidth(qMax(100, btn->fontMetrics().horizontalAdvance(tabText) + 46));

    connect(btn, &QPushButton::clicked, this, [this, btn]() {
        for (int i = 0; i < m_tabBtns.size(); ++i) {
            if (m_tabBtns[i] == btn) {
                if (m_tabEmpty[i]) Q_EMIT openFileRequested();
                else { setCurrentTab(i); Q_EMIT tabActivated(i); }
                return;
            }
        }
    });

    connect(closeBtn, &QPushButton::clicked, this, [this, closeBtn](bool) {
        for (int i = 0; i < m_tabBtns.size(); ++i) {
            if (m_tabBtns[i]->layout()->indexOf(closeBtn) >= 0) {
                Q_EMIT tabCloseRequested(i);
                return;
            }
        }
    });

    m_tabBtns.append(btn);
    m_tabLabels.append(lbl);
    m_tabEmpty.append(empty);

    // Insert BEFORE "+" — keeps "+" to the right of all tabs
    m_tabLayout->insertWidget(m_tabBtns.size() - 1, btn);

    syncTabBarWidth();
    setCurrentTab(idx);
    scrollToTab(idx);
    return idx;
}

void TopToolbar::removeTab(int index)
{
    if (index < 0 || index >= m_tabBtns.size()) return;
    QPushButton *btn = m_tabBtns.takeAt(index);
    m_tabLabels.removeAt(index);
    m_tabEmpty.removeAt(index);
    m_tabLayout->removeWidget(btn);
    btn->deleteLater();

    syncTabBarWidth();

    if (!m_tabBtns.isEmpty()) {
        const int next = qMin(index, m_tabBtns.size() - 1);
        setCurrentTab(next);
        scrollToTab(next);
    } else {
        m_currentTab = -1;
    }
}

void TopToolbar::setTabLabel(int index, const QString &label)
{
    if (index < 0 || index >= m_tabLabels.size()) return;
    const bool empty = label.isEmpty();
    m_tabEmpty[index] = empty;
    const QString text = empty ? tr("No Document") : label;
    m_tabLabels[index]->setText(text);
    m_tabBtns[index]->setFixedWidth(
        qMax(100, m_tabBtns[index]->fontMetrics().horizontalAdvance(text) + 46));
    syncTabBarWidth();
}

void TopToolbar::setCurrentTab(int index)
{
    if (index < 0 || index >= m_tabBtns.size()) return;
    m_currentTab = index;
    for (int i = 0; i < m_tabBtns.size(); ++i)
        m_tabBtns[i]->setChecked(i == index);
}

// ── Private helpers ───────────────────────────────────────────────────────────

void TopToolbar::syncTabBarWidth()
{
    int total = 2;
    for (auto *btn : m_tabBtns)
        total += btn->width() + m_tabLayout->spacing();
    total += 32 + 4; // "+" button + margin
    m_tabBar->setFixedWidth(qMax(total, 2));
}

void TopToolbar::scrollToTab(int index)
{
    if (!m_tabScroll || index < 0 || index >= m_tabBtns.size()) return;
    QScrollBar *hbar = m_tabScroll->horizontalScrollBar();
    const int btnX = m_tabBtns[index]->x();
    const int btnW = m_tabBtns[index]->width();
    const int vpW  = m_tabScroll->viewport()->width();
    if (btnX < hbar->value())
        hbar->setValue(btnX);
    else if (btnX + btnW > hbar->value() + vpW)
        hbar->setValue(btnX + btnW - vpW);
}

// ── Layout ────────────────────────────────────────────────────────────────────

void TopToolbar::buildLayout()
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(2);

    // Logo
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

    // ── Scrollable tab area ────────────────────────────────────────────────
    m_tabScroll = new QScrollArea(this);
    m_tabScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tabScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tabScroll->setFrameShape(QFrame::NoFrame);
    m_tabScroll->setFixedHeight(42);
    m_tabScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_tabScroll->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_tabScroll->setStyleSheet(QStringLiteral("background:transparent;"));

    m_tabBar = new QWidget;
    m_tabBar->setObjectName(QStringLiteral("TabBarInner"));
    m_tabBar->setAttribute(Qt::WA_StyledBackground, false);

    m_tabLayout = new QHBoxLayout(m_tabBar);
    m_tabLayout->setContentsMargins(0, 4, 0, 4);
    m_tabLayout->setSpacing(2);

    // "+" button – stays as the first/only item initially; tabs insert before it
    m_newTabBtn = new IconButton(m_tabBar);
    m_newTabBtn->setObjectName(QStringLiteral("NewTabBtn"));
    m_newTabBtn->setIconName(QStringLiteral("plus"), QColor("#9CA3AF"));
    m_newTabBtn->setFixedSize(28, 28);
    m_newTabBtn->setIconSize(QSize(16, 16));
    m_newTabBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_newTabBtn, &QPushButton::clicked, this, &TopToolbar::newTabRequested);
    m_tabLayout->addWidget(m_newTabBtn);

    m_tabBar->setFixedHeight(38);
    m_tabBar->setFixedWidth(60);
    m_tabScroll->setWidget(m_tabBar);

    layout->addWidget(m_tabScroll, 1);

    layout->addSpacing(4);
    layout->addWidget(makeSeparator());
    layout->addSpacing(4);

    // Save / Print
    m_saveBtn = new IconButton(this);
    m_saveBtn->setIconName(QStringLiteral("save"));
    m_saveBtn->setToolTip(tr("Save"));
    connect(m_saveBtn, &QPushButton::clicked, this, &TopToolbar::saveRequested);
    layout->addWidget(m_saveBtn);

    m_printBtn = new IconButton(this);
    m_printBtn->setIconName(QStringLiteral("printer"));
    m_printBtn->setToolTip(tr("Print"));
    connect(m_printBtn, &QPushButton::clicked, this, &TopToolbar::printRequested);
    layout->addWidget(m_printBtn);

    layout->addSpacing(2);
    layout->addWidget(makeSeparator());
    layout->addSpacing(2);

    // Undo / Redo
    m_undoBtn = new IconButton(this);
    m_undoBtn->setIconName(QStringLiteral("undo-2"));
    m_undoBtn->setToolTip(tr("Undo"));
    connect(m_undoBtn, &QPushButton::clicked, this, &TopToolbar::undoRequested);
    layout->addWidget(m_undoBtn);

    m_redoBtn = new IconButton(this);
    m_redoBtn->setIconName(QStringLiteral("redo-2"));
    m_redoBtn->setToolTip(tr("Redo"));
    connect(m_redoBtn, &QPushButton::clicked, this, &TopToolbar::redoRequested);
    layout->addWidget(m_redoBtn);

    layout->addSpacing(2);
    layout->addWidget(makeSeparator());
    layout->addSpacing(2);

    // Zoom
    m_zoomOutBtn = new IconButton(this);
    m_zoomOutBtn->setIconName(QStringLiteral("zoom-out"));
    m_zoomOutBtn->setToolTip(tr("Zoom Out"));
    connect(m_zoomOutBtn, &QPushButton::clicked, this, &TopToolbar::zoomOutRequested);
    layout->addWidget(m_zoomOutBtn);

    m_zoomLabel = new QLabel(QStringLiteral("100 %"), this);
    m_zoomLabel->setObjectName(QStringLiteral("ZoomLabel"));
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setMinimumWidth(52);
    layout->addWidget(m_zoomLabel);

    m_zoomInBtn = new IconButton(this);
    m_zoomInBtn->setIconName(QStringLiteral("zoom-in"));
    m_zoomInBtn->setToolTip(tr("Zoom In"));
    connect(m_zoomInBtn, &QPushButton::clicked, this, &TopToolbar::zoomInRequested);
    layout->addWidget(m_zoomInBtn);

    layout->addSpacing(2);
    layout->addWidget(makeSeparator());
    layout->addSpacing(2);

    // View toggles — radio pair
    m_viewSingleBtn = new IconButton(this);
    m_viewSingleBtn->setIconName(QStringLiteral("square"));
    m_viewSingleBtn->setToolTip(tr("Single Page"));
    m_viewSingleBtn->setCheckable(true);
    m_viewSingleBtn->setChecked(true);
    connect(m_viewSingleBtn, &QPushButton::clicked, this, [this]() {
        m_viewSingleBtn->setChecked(true);
        m_viewGridBtn->setChecked(false);
        Q_EMIT viewModeChanged(false);
    });
    layout->addWidget(m_viewSingleBtn);

    m_viewGridBtn = new IconButton(this);
    m_viewGridBtn->setIconName(QStringLiteral("layout-grid"));
    m_viewGridBtn->setToolTip(tr("Grid View"));
    m_viewGridBtn->setCheckable(true);
    connect(m_viewGridBtn, &QPushButton::clicked, this, [this]() {
        m_viewGridBtn->setChecked(true);
        m_viewSingleBtn->setChecked(false);
        Q_EMIT viewModeChanged(true);
    });
    layout->addWidget(m_viewGridBtn);

    // Tabs are added by MainWindow after construction
}

QWidget *TopToolbar::makeSeparator()
{
    auto *sep = new QFrame(this);
    sep->setObjectName(QStringLiteral("ToolbarSeparator"));
    sep->setFrameShape(QFrame::NoFrame);
    sep->setFixedWidth(1);
    sep->setFixedHeight(22);
    return sep;
}
