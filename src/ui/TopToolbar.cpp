#include "TopToolbar.hpp"
#include "ui/widgets/IconButton.hpp"
#include "ui/theme/Theme.hpp"

#include <QHBoxLayout>
#include <QLabel>
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
    if (m_currentTab >= 0 && m_currentTab < m_tabBtns.size())
        setTabLabel(m_currentTab, name);
}

void TopToolbar::setZoom(int percent)
{
    m_zoomLabel->setText(QStringLiteral("%1 %").arg(percent));
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
}

// ── Tab management ────────────────────────────────────────────────────────────

int TopToolbar::addTab(const QString &label)
{
    const int idx   = m_tabBtns.size();
    const bool empty = label.isEmpty();

    auto *btn = new QPushButton(this);
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

    // Width based on text content
    btn->setFixedWidth(qMax(100, btn->fontMetrics().horizontalAdvance(tabText) + 46));

    // Tab click → activate or open file
    connect(btn, &QPushButton::clicked, this, [this, btn]() {
        for (int i = 0; i < m_tabBtns.size(); ++i) {
            if (m_tabBtns[i] == btn) {
                if (m_tabEmpty[i]) Q_EMIT openFileRequested();
                else { setCurrentTab(i); Q_EMIT tabActivated(i); }
                return;
            }
        }
    });

    // X click → close
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

    // Insert before "+" button — keeps "+" to the right
    m_tabLayout->insertWidget(m_tabBtns.size() - 1, btn);

    setCurrentTab(idx);
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

    if (!m_tabBtns.isEmpty())
        setCurrentTab(qMin(index, m_tabBtns.size() - 1));
    else
        m_currentTab = -1;
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
}

void TopToolbar::setCurrentTab(int index)
{
    if (index < 0 || index >= m_tabBtns.size()) return;
    m_currentTab = index;
    for (int i = 0; i < m_tabBtns.size(); ++i)
        m_tabBtns[i]->setChecked(i == index);
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

    // Tab bar
    m_tabBar = new QWidget(this);
    m_tabBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_tabLayout = new QHBoxLayout(m_tabBar);
    m_tabLayout->setContentsMargins(0, 0, 0, 0);
    m_tabLayout->setSpacing(2);

    // "+" button always rightmost
    auto *newTabBtn = new IconButton(m_tabBar);
    newTabBtn->setObjectName(QStringLiteral("NewTabBtn"));
    newTabBtn->setIconName(QStringLiteral("plus"), QColor("#9CA3AF"));
    newTabBtn->setFixedSize(28, 28);
    newTabBtn->setIconSize(QSize(16, 16));
    newTabBtn->setFocusPolicy(Qt::NoFocus);
    connect(newTabBtn, &QPushButton::clicked, this, &TopToolbar::newTabRequested);
    m_tabLayout->addWidget(newTabBtn);
    m_tabLayout->addStretch(1);

    layout->addWidget(m_tabBar, 1);

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

    // View toggles
    m_viewSingleBtn = new IconButton(this);
    m_viewSingleBtn->setIconName(QStringLiteral("square"));
    m_viewSingleBtn->setToolTip(tr("Single Page"));
    m_viewSingleBtn->setCheckable(true);
    m_viewSingleBtn->setChecked(true);
    layout->addWidget(m_viewSingleBtn);

    m_viewGridBtn = new IconButton(this);
    m_viewGridBtn->setIconName(QStringLiteral("layout-grid"));
    m_viewGridBtn->setToolTip(tr("Grid View"));
    m_viewGridBtn->setCheckable(true);
    layout->addWidget(m_viewGridBtn);

    // Initial empty tab
    addTab();
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
