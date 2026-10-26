#include "ui/settings/SettingsPanel.hpp"
#include "app/AppSettings.hpp"
#include "app/LogFile.hpp"
#include "drm/LicenseStore.hpp"
#include "drm/LicensePage.hpp"
#include "ui/theme/Theme.hpp"
#include "ui/settings/LangRow.hpp"
#include "ui/settings/OptionCard.hpp"
#include "ui/settings/SettingsSearch.hpp"
#include "ui/settings/SettingCheckBox.hpp"
#include "ui/settings/ShortcutRow.hpp"

#include <QAbstractButton>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

SettingsPanel::SettingsPanel(AppSettings *settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_pendingTheme(settings->theme())
    , m_pendingLang(settings->language())
    , m_originalTheme(settings->theme())
    , m_originalLang(settings->language())
{
    setObjectName(QStringLiteral("SettingsPanel"));
    setWindowTitle(tr("Settings - OpenPDF Studio"));
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(true);
    setFixedSize(900, 660);

    buildUi();

    connect(this, &QDialog::rejected, this, [this]() {
        if (m_pendingTheme != m_originalTheme)
            Q_EMIT themeChangeRequested(m_originalTheme);
    });
}

void SettingsPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *contentRow = new QHBoxLayout;
    contentRow->setContentsMargins(0, 0, 0, 0);
    contentRow->setSpacing(0);

    auto *sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("SettingsSidebar"));
    sidebar->setFixedWidth(190);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(8, 12, 8, 12);
    sideLayout->setSpacing(2);
    buildNav(sidebar, sideLayout);
    contentRow->addWidget(sidebar);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("SettingsPages"));
    m_pages->addWidget(buildAppearancePage());
    m_pages->addWidget(buildLanguagePage());
    m_pages->addWidget(buildMediaPage());
    m_pages->addWidget(buildShortcutsPage());
    m_pages->addWidget(buildZoomPage());
    m_pages->addWidget(buildSecurityPage());
    m_pages->addWidget(buildAdvancedPage());

    if (License::isBusinessInstall()) {
        m_licensePage = new LicensePage(this);
        m_pages->addWidget(m_licensePage);
    }
    m_pages->addWidget(buildAboutPage());

    Q_ASSERT(m_pages->count() == m_navItems.size());

    m_search = new SettingsSearch(m_pages, this);
    sideLayout->insertWidget(0, m_search->field());
    sideLayout->insertWidget(1, m_search->noResults());
    sideLayout->insertSpacing(2, 8);
    connect(m_search, &SettingsSearch::filtered, this, &SettingsPanel::showSearchHits);

    contentRow->addWidget(m_pages, 1);
    root->addLayout(contentRow, 1);

    auto *sep = new QFrame(this);
    sep->setObjectName(QStringLiteral("SettingsSeparator"));
    sep->setFrameShape(QFrame::HLine);
    root->addWidget(sep);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(16, 10, 16, 10);
    bar->setSpacing(8);

    bar->addStretch(1);

    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setObjectName(QStringLiteral("SettingsCancelBtn"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    bar->addWidget(cancelBtn);

    auto *saveBtn = new QPushButton(tr("Save"), this);
    saveBtn->setObjectName(QStringLiteral("SettingsSaveBtn"));
    saveBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, &SettingsPanel::applyAndClose);
    bar->addWidget(saveBtn);

    root->addLayout(bar);

    selectPage(0);
}

void SettingsPanel::buildNav(QWidget *, QVBoxLayout *layout)
{
    struct Item { const char *icon; const char *label; };
    QList<Item> items = {
        { "monitor",     QT_TR_NOOP("Appearance")     },
        { "globe",       QT_TR_NOOP("Language")       },
        { "play-circle", QT_TR_NOOP("Media Playback") },
        { "keyboard",    QT_TR_NOOP("Shortcuts")      },
        { "zoom-in",     QT_TR_NOOP("Zoom")           },
        { "shield",      QT_TR_NOOP("Security")       },
        { "sliders",     QT_TR_NOOP("Advanced")       },
    };

    if (License::isBusinessInstall())
        items.append(Item{ "key", QT_TR_NOOP("License Key") });

    for (int i = 0; i < items.size(); ++i) {
        auto *btn = new QPushButton(this);
        btn->setFixedHeight(38);
        btn->setFlat(true);
        btn->setCursor(Qt::PointingHandCursor);

        auto *row = new QHBoxLayout(btn);
        row->setContentsMargins(10, 0, 10, 0);
        row->setSpacing(8);

        auto *iconLbl = new QLabel(btn);
        iconLbl->setFixedSize(18, 18);
        iconLbl->setAlignment(Qt::AlignCenter);
        iconLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(iconLbl);

        auto *textLbl = new QLabel(tr(items[i].label), btn);
        textLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(textLbl, 1);

        m_navItems.append({ btn, iconLbl, textLbl,
                            QLatin1String(items[i].icon), items[i].label });

        const int navIdx = i;
        connect(btn, &QPushButton::clicked, this, [this, navIdx]() { selectPage(navIdx); });
        layout->addWidget(btn);
    }

    layout->addStretch(1);

    auto *aboutBtn = new QPushButton(this);
    aboutBtn->setFixedHeight(38);
    aboutBtn->setFlat(true);
    aboutBtn->setCursor(Qt::PointingHandCursor);
    auto *arow = new QHBoxLayout(aboutBtn);
    arow->setContentsMargins(10, 0, 10, 0);
    arow->setSpacing(8);
    auto *aicon = new QLabel(aboutBtn);
    aicon->setFixedSize(18, 18);
    aicon->setAttribute(Qt::WA_TransparentForMouseEvents);
    arow->addWidget(aicon);
    auto *atext = new QLabel(tr("About"), aboutBtn);
    atext->setAttribute(Qt::WA_TransparentForMouseEvents);
    arow->addWidget(atext, 1);
    const int aboutIdx = m_navItems.size();
    m_navItems.append({ aboutBtn, aicon, atext, QStringLiteral("info"),
                        QT_TR_NOOP("About") });
    connect(aboutBtn, &QPushButton::clicked, this, [this, aboutIdx]() { selectPage(aboutIdx); });
    layout->addWidget(aboutBtn);
}

void SettingsPanel::applyNavItemStyle(int i, bool sel)
{
    if (i < 0 || i >= m_navItems.size()) return;
    NavItem &ni = m_navItems[i];
    const bool dk = Theme::DarkMode;

    const QString selBg  = dk ? "#1E3A5F" : "#DBEAFE";
    const QString selHov = dk ? "#2A4A70" : "#BFDBFE";
    const QString norHov = dk ? "#3E3E3E" : "#F3F4F6";
    const QString selTxt = dk ? "#93C5FD" : "#1D4ED8";
    const QString norTxt = dk ? "#C8C8C8" : "#374151";

    const QPixmap px = Theme::renderSvg(ni.iconName,
        QColor(sel ? (dk ? "#93C5FD" : "#3B82F6") : (dk ? "#888888" : "#6B7280")), 16);
    if (!px.isNull()) ni.iconLabel->setPixmap(px);

    ni.textLabel->setStyleSheet(sel
        ? QStringLiteral("font-size:13px; font-weight:600; color:%1;").arg(selTxt)
        : QStringLiteral("font-size:13px; color:%1;").arg(norTxt));

    ni.btn->setStyleSheet(sel
        ? QStringLiteral(
            "QPushButton { border-radius:6px; background:%1; border:none; }"
            "QPushButton:hover { background:%2; }").arg(selBg, selHov)
        : QStringLiteral(
            "QPushButton { border-radius:6px; background:transparent; border:none; }"
            "QPushButton:hover { background:%1; }").arg(norHov));
}

void SettingsPanel::selectPage(int navIndex)
{
    m_currentNav = navIndex;
    m_pages->setCurrentIndex(navIndex);
    for (int i = 0; i < m_navItems.size(); ++i)
        applyNavItemStyle(i, i == navIndex);
}

void SettingsPanel::showSearchHits(const QList<int> &pages)
{
    for (int i = 0; i < m_navItems.size(); ++i)
        m_navItems[i].btn->setVisible(pages.contains(i));
    if (!pages.isEmpty())
        selectPage(pages.first());
    m_search->reveal(m_currentNav);
}

void SettingsPanel::showLicensePage()
{
    if (m_licensePage)
        selectPage(m_pages->indexOf(m_licensePage));
}

void SettingsPanel::selectPageForTest(const QString &navLabel)
{
    for (int i = 0; i < m_navItems.size(); ++i)
        if (navLabel.compare(QLatin1String(m_navItems[i].labelKey),
                             Qt::CaseInsensitive) == 0) {
            selectPage(i);
            return;
        }
}

void SettingsPanel::refreshThemeColors()
{
    for (int i = 0; i < m_navItems.size(); ++i)
        applyNavItemStyle(i, i == m_currentNav);
    for (auto *w : m_themeCards)
        if (auto *c = qobject_cast<OptionCard*>(w)) c->setSelected(c->isSelected());
    for (auto *w : m_mediaCards)
        if (auto *c = qobject_cast<OptionCard*>(w)) c->setSelected(c->isSelected());
    for (auto *w : m_langRows)
        if (auto *r = qobject_cast<LangRow*>(w)) r->setSelected(r->isSelected());
    if (m_licensePage) m_licensePage->applyTheme();
    if (m_search) m_search->applyTheme();
}

QVBoxLayout *SettingsPanel::buildScrollPage(QWidget *&page, const QString &title,
                                            const QString &desc)
{
    page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *outerLayout = new QVBoxLayout(page);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("SettingsScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidgetResizable(true);
    outerLayout->addWidget(scroll);

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vl = new QVBoxLayout(content);
    vl->setContentsMargins(32, 28, 32, 32);
    vl->setSpacing(12);

    scroll->setWidget(content);

    auto *titleLabel = new QLabel(title, content);
    titleLabel->setObjectName(QStringLiteral("SettingsSectionTitle"));
    vl->addWidget(titleLabel);
    vl->addSpacing(4);

    auto *descLabel = new QLabel(desc, content);
    descLabel->setObjectName(QStringLiteral("SettingsSectionDesc"));
    vl->addWidget(descLabel);
    vl->addSpacing(16);
    return vl;
}

void SettingsPanel::finishScrollPage(QVBoxLayout *contentLayout)
{
    contentLayout->addStretch(1);
}

QVBoxLayout *SettingsPanel::addSettingsCard(QVBoxLayout *into, const QString &cardTitle)
{
    auto *card = new QFrame(into->parentWidget());
    card->setObjectName(QStringLiteral("SettingsCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto *cl = new QVBoxLayout(card);
    cl->setContentsMargins(16, 16, 16, 16);
    cl->setSpacing(8);
    auto *t = new QLabel(cardTitle, card);
    t->setObjectName(QStringLiteral("SettingsCardTitle"));
    cl->addWidget(t);
    cl->addSpacing(4);
    into->addWidget(card);
    return cl;
}

QAbstractButton *SettingsPanel::addSettingsCheck(QVBoxLayout *cl, const QString &label,
                                             const QString &explain, bool checked)
{
    QWidget *card = cl->parentWidget();
    auto *box = new SettingCheckBox(label, card);
    box->setChecked(checked);
    cl->addWidget(box, 0, Qt::AlignLeft);
    auto *d = new QLabel(explain, card);
    d->setObjectName(QStringLiteral("SettingsRowDesc"));
    d->setWordWrap(true);
    d->setContentsMargins(SettingCheckBox::kBox + SettingCheckBox::kSpacing, 0, 0, 0);
    cl->addWidget(d);
    return box;
}

QComboBox *SettingsPanel::addSettingsCombo(QVBoxLayout *cl, const QString &label)
{
    QWidget *card = cl->parentWidget();
    auto *row = new QHBoxLayout;
    row->setContentsMargins(SettingCheckBox::kBox + SettingCheckBox::kSpacing,
                            8, 0, 0);
    row->setSpacing(12);
    auto *l = new QLabel(label, card);
    l->setObjectName(QStringLiteral("SettingsRowLabel"));
    row->addWidget(l);
    row->addStretch(1);
    auto *combo = new QComboBox(card);
    combo->setObjectName(QStringLiteral("AdvancedCombo"));
    combo->setFixedWidth(220);
    row->addWidget(combo);
    cl->addLayout(row);
    return combo;
}

void SettingsPanel::selectComboData(QComboBox *combo, const QString &value)
{
    for (int i = 0; i < combo->count(); ++i)
        if (combo->itemData(i).toString() == value) {
            combo->setCurrentIndex(i);
            return;
        }
}

QWidget *SettingsPanel::buildOptionCard(const QString &icon, const QString &title,
                                         const QString &desc,  const QString &id,
                                         QList<QWidget*> &group, QList<QString> &ids,
                                         bool isThemeGroup)
{
    auto *card = new OptionCard(icon, title, desc, this);

    if (isThemeGroup)
        card->setSelected(id == m_pendingTheme);
    else
        card->setSelected(id == QStringLiteral("default"));

    group.append(card);
    ids.append(id);

    connect(card, &OptionCard::clicked, this, [this, id, &group, &ids, isThemeGroup]() {
        selectCardGroup(id, group, ids);
        if (isThemeGroup) {
            m_pendingTheme = id;
            Q_EMIT themeChangeRequested(id);
        } else if (&group == &m_mediaCards) {
            m_pendingMedia = id;
            if (m_customPlayerRow)
                m_customPlayerRow->setEnabled(id == QLatin1String("custom"));
        }
    });
    return card;
}

void SettingsPanel::selectCardGroup(const QString &id,
                                     QList<QWidget*> &cards, QList<QString> &ids)
{
    for (int i = 0; i < cards.size(); ++i)
        qobject_cast<OptionCard *>(cards[i])->setSelected(ids[i] == id);
}

void SettingsPanel::applyAndClose()
{
    m_settings->setTheme(m_pendingTheme);
    m_settings->setLanguage(m_pendingLang);
    if (!m_pendingMedia.isEmpty())
        m_settings->setMediaPlayback(m_pendingMedia);
    if (m_customPlayerEdit)
        m_settings->setCustomPlayerCommand(m_customPlayerEdit->text().trimmed());

    for (ShortcutRow *r : m_shortcutRows) r->cancelIfEditing();

    for (int i = 0; i < m_shortcutRows.size() && i < m_shortcutKeys.size(); ++i)
        m_settings->setShortcut(m_shortcutKeys[i], m_shortcutRows[i]->currentSequence());

    if (m_zoomStepSpin) {
        m_settings->setZoomStep(m_zoomStepSpin->value());
        m_settings->setCtrlWheelZoom(m_ctrlWheelToggle && m_ctrlWheelToggle->isChecked());
        m_settings->setZoomToPointer(m_zoomPtrToggle && m_zoomPtrToggle->isChecked());
        if (m_wheelActionCombo)
            m_settings->setWheelAction(m_wheelActionCombo->currentData().toString());
    }

    bool panelLayoutChanged = false;
    if (m_preserveLayoutCheck) {
        const bool preserve = m_preserveLayoutCheck->isChecked();
        panelLayoutChanged = (preserve != m_settings->preservePanelLayout());
        m_settings->setPreservePanelLayout(preserve);
    }

    if (m_readModeCheck)
        m_settings->setOpenInReadMode(m_readModeCheck->isChecked());
    if (m_externalLinksCombo)
        m_settings->setExternalLinks(m_externalLinksCombo->currentData().toString());
    if (m_permissionsCheck)
        m_settings->setRespectPermissions(m_permissionsCheck->isChecked());
    bool trustChanged = false;
    if (m_trustOwnCheck) {
        const bool trustOwn = m_trustOwnCheck->isChecked();
        trustChanged = trustOwn != m_settings->trustOwnCertificates();
        m_settings->setTrustOwnCertificates(trustOwn);
    }

    if (m_autoUpdateCheck)
        m_settings->setAutoUpdateCheck(m_autoUpdateCheck->isChecked());
    if (m_updateIntervalCombo)
        m_settings->setUpdateInterval(m_updateIntervalCombo->currentData().toString());
    if (m_debugLogCheck)
        m_settings->setDebugLogging(m_debugLogCheck->isChecked());
    if (m_logLevelCombo)
        m_settings->setLogLevel(m_logLevelCombo->currentData().toString());
    LogFile::configure(m_settings->debugLogging(), m_settings->logLevel());

    m_settings->sync();

    if (m_pendingLang != m_originalLang)
        Q_EMIT languageChangeRequested(m_pendingLang);

    Q_EMIT shortcutsChanged();
    Q_EMIT zoomSettingsChanged();

    if (panelLayoutChanged)
        Q_EMIT panelLayoutSettingChanged();
    if (trustChanged)
        Q_EMIT signatureTrustChanged();

    accept();
}

void SettingsPanel::retranslateUi()
{
    setWindowTitle(tr("Settings - OpenPDF Studio"));

    for (const NavItem &ni : m_navItems)
        ni.textLabel->setText(tr(ni.labelKey));
    if (m_search) m_search->retranslate();
}

void SettingsPanel::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    else if (e->type() == QEvent::StyleChange)
        refreshThemeColors();
    QDialog::changeEvent(e);
}
