#include "SettingsPanel.hpp"
#include "app/AppSettings.hpp"
#include "ui/theme/Theme.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QStackedWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <QEvent>
#include <QMouseEvent>
#include <QApplication>

// ── OptionCard ────────────────────────────────────────────────────────────────

class OptionCard : public QFrame
{
    Q_OBJECT
public:
    explicit OptionCard(const QString &iconName, const QString &title,
                        const QString &desc, QWidget *parent = nullptr)
        : QFrame(parent), m_iconName(iconName)
    {
        setFixedSize(175, 112);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, true);

        auto *vbox = new QVBoxLayout(this);
        vbox->setContentsMargins(12, 12, 12, 10);
        vbox->setSpacing(5);
        vbox->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

        m_iconLabel = new QLabel(this);
        m_iconLabel->setFixedSize(26, 26);
        m_iconLabel->setAlignment(Qt::AlignCenter);
        m_iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        vbox->addWidget(m_iconLabel, 0, Qt::AlignHCenter);

        m_titleLabel = new QLabel(title, this);
        m_titleLabel->setAlignment(Qt::AlignCenter);
        m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        vbox->addWidget(m_titleLabel);

        m_descLabel = new QLabel(desc, this);
        m_descLabel->setAlignment(Qt::AlignCenter);
        m_descLabel->setWordWrap(true);
        m_descLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        vbox->addWidget(m_descLabel);

        m_check = new QLabel(QStringLiteral("✓"), this);
        m_check->setFixedSize(20, 20);
        m_check->setAlignment(Qt::AlignCenter);
        m_check->hide();

        applyStyle(false);
    }

    bool isSelected() const { return m_selected; }
    void setSelected(bool v) { m_selected = v; m_check->setVisible(v); applyStyle(v); }
    const QString &iconName() const { return m_iconName; }

Q_SIGNALS:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) Q_EMIT clicked();
    }
    void resizeEvent(QResizeEvent *e) override
    {
        QFrame::resizeEvent(e);
        m_check->move(width() - 26, 5);
    }

private:
    void applyStyle(bool sel)
    {
        const bool dk = Theme::DarkMode;
        const QColor iconColor = sel ? QColor("#3B82F6")
                                     : QColor(dk ? "#9CA3AF" : "#6B7280");
        const QPixmap px = Theme::renderSvg(m_iconName, iconColor, 22);
        if (!px.isNull()) m_iconLabel->setPixmap(px);

        if (sel) {
            const QString bg = dk ? "#1E3358" : "#FFFFFF";
            setStyleSheet(QStringLiteral(
                "OptionCard { border:2px solid #3B82F6; border-radius:8px; background:%1; }").arg(bg));
            m_check->setStyleSheet(QStringLiteral(
                "background:#3B82F6; border-radius:10px; color:white; font-size:12px; font-weight:700;"));
            m_titleLabel->setStyleSheet(dk
                ? QStringLiteral("font-size:12px; font-weight:600; color:#93C5FD;")
                : QStringLiteral("font-size:12px; font-weight:600; color:#1D4ED8;"));
            m_descLabel->setStyleSheet(dk
                ? QStringLiteral("font-size:10px; color:#60A5FA;")
                : QStringLiteral("font-size:10px; color:#3B82F6;"));
        } else {
            const QString bg  = dk ? "#404040" : "#FFFFFF";
            const QString bdr = dk ? "#555555" : "#E5E7EB";
            setStyleSheet(QStringLiteral(
                "OptionCard { border:1px solid %1; border-radius:8px; background:%2; }").arg(bdr, bg));
            m_titleLabel->setStyleSheet(dk
                ? QStringLiteral("font-size:12px; font-weight:600; color:#D0D0D0;")
                : QStringLiteral("font-size:12px; font-weight:600; color:#111827;"));
            m_descLabel->setStyleSheet(dk
                ? QStringLiteral("font-size:10px; color:#9CA3AF;")
                : QStringLiteral("font-size:10px; color:#6B7280;"));
        }
    }

    QLabel *m_iconLabel  { nullptr };
    QLabel *m_titleLabel { nullptr };
    QLabel *m_descLabel  { nullptr };
    QLabel *m_check      { nullptr };
    QString m_iconName;
    bool    m_selected   { false };
};

#include "SettingsPanel.moc"

// ── SettingsPanel ─────────────────────────────────────────────────────────────

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
    setFixedSize(820, 580);

    buildUi();

    connect(this, &QDialog::rejected, this, [this]() {
        if (m_pendingTheme != m_originalTheme)
            Q_EMIT themeChangeRequested(m_originalTheme);
    });
}

// ── UI ────────────────────────────────────────────────────────────────────────

void SettingsPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *contentRow = new QHBoxLayout;
    contentRow->setContentsMargins(0, 0, 0, 0);
    contentRow->setSpacing(0);

    // ── Left sidebar ──────────────────────────────────────────────────────
    auto *sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("SettingsSidebar"));
    sidebar->setFixedWidth(190);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(8, 12, 8, 12);
    sideLayout->setSpacing(2);
    buildNav(sidebar, sideLayout);
    contentRow->addWidget(sidebar);

    // ── Right: stacked pages ──────────────────────────────────────────────
    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("SettingsPages"));

    // Page 0: scrollable settings
    m_generalScroll = new QScrollArea(this);
    m_generalScroll->setObjectName(QStringLiteral("SettingsScroll"));
    m_generalScroll->setFrameShape(QFrame::NoFrame);
    m_generalScroll->setWidgetResizable(true);
    m_generalScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_generalScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto *scrollContent = new QWidget;
    scrollContent->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(28, 24, 28, 32);
    scrollLayout->setSpacing(0);

    buildSection_Appearance(scrollContent, scrollLayout);
    makeSeparator(scrollContent, scrollLayout);
    buildSection_Language(scrollContent, scrollLayout);
    makeSeparator(scrollContent, scrollLayout);
    buildSection_MediaPlayback(scrollContent, scrollLayout);
    makeSeparator(scrollContent, scrollLayout);
    buildSection_Advanced(scrollContent, scrollLayout);
    scrollLayout->addStretch(1);

    m_generalScroll->setWidget(scrollContent);
    m_pages->addWidget(m_generalScroll);    // page 0
    m_pages->addWidget(buildAboutPage());   // page 1

    contentRow->addWidget(m_pages, 1);
    root->addLayout(contentRow, 1);

    // ── Bottom bar ────────────────────────────────────────────────────────
    auto *sep = new QFrame(this);
    sep->setObjectName(QStringLiteral("SettingsSeparator"));
    sep->setFrameShape(QFrame::HLine);
    root->addWidget(sep);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(16, 10, 16, 10);
    bar->setSpacing(8);

    auto *resetBtn = new QPushButton(tr("Reset"), this);
    resetBtn->setObjectName(QStringLiteral("SettingsResetBtn"));
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        m_pendingTheme = QStringLiteral("system");
        m_pendingLang  = QStringLiteral("en");
        selectCardGroup(m_pendingTheme, m_themeCards, m_themeIds);
        Q_EMIT themeChangeRequested(m_pendingTheme);
        if (m_langCombo) {
            const int idx = m_langCombo->findData(m_pendingLang);
            if (idx >= 0) m_langCombo->setCurrentIndex(idx);
        }
    });
    bar->addWidget(resetBtn);
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

    selectPage(0, 0);
}

// ── Nav ───────────────────────────────────────────────────────────────────────

void SettingsPanel::buildNav(QWidget *, QVBoxLayout *layout)
{
    struct Item { const char *icon; const char *label; int anchor; };
    // anchor: 0=top(Appearance), 1=Language, 2=Media, 3=Advanced
    const Item items[] = {
        { "settings",    QT_TR_NOOP("General"),        0 },
        { "monitor",     QT_TR_NOOP("Appearance"),     0 },
        { "globe",       QT_TR_NOOP("Language"),       1 },
        { "play-circle", QT_TR_NOOP("Media Playback"), 2 },
        { "sliders",     QT_TR_NOOP("Advanced"),       3 },
    };

    for (int i = 0; i < 5; ++i) {
        const auto &item = items[i];
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

        auto *textLbl = new QLabel(tr(item.label), btn);
        textLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(textLbl, 1);

        m_navItems.append({ btn, iconLbl, textLbl, QLatin1String(item.icon), item.anchor });

        const int navIdx = i;
        const int anchor = item.anchor;
        connect(btn, &QPushButton::clicked, this, [this, navIdx, anchor]() {
            selectPage(navIdx, anchor, true);
        });
        layout->addWidget(btn);
    }

    layout->addStretch(1);

    // About (navIdx 5)
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
    m_navItems.append({ aboutBtn, aicon, atext, QStringLiteral("info"), -1 });
    connect(aboutBtn, &QPushButton::clicked, this, [this]() { selectPage(5, -1, false); });
    layout->addWidget(aboutBtn);
}

// ── Nav highlight ─────────────────────────────────────────────────────────────

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
    const QString selIc  = dk ? "#93C5FD" : "#3B82F6";
    const QString norIc  = dk ? "#888888" : "#6B7280";

    const QPixmap px = Theme::renderSvg(ni.iconName, QColor(sel ? selIc : norIc), 16);
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

void SettingsPanel::selectPage(int navIndex, int anchorIndex, bool scrollToAnchor)
{
    m_currentNav = navIndex;
    const bool isAbout = (anchorIndex < 0);
    m_pages->setCurrentIndex(isAbout ? 1 : 0);

    for (int i = 0; i < m_navItems.size(); ++i)
        applyNavItemStyle(i, i == navIndex);

    if (scrollToAnchor && !isAbout && m_generalScroll) {
        switch (anchorIndex) {
        case 0: m_generalScroll->verticalScrollBar()->setValue(0); break;
        case 1: if (m_langAnchor)  m_generalScroll->ensureWidgetVisible(m_langAnchor,  0, 8); break;
        case 2: if (m_mediaAnchor) m_generalScroll->ensureWidgetVisible(m_mediaAnchor, 0, 8); break;
        case 3: if (m_advAnchor)   m_generalScroll->ensureWidgetVisible(m_advAnchor,   0, 8); break;
        }
    }
}

void SettingsPanel::refreshThemeColors()
{
    // Re-apply nav button colors with current theme
    for (int i = 0; i < m_navItems.size(); ++i)
        applyNavItemStyle(i, i == m_currentNav);

    // Re-render OptionCards (they read Theme::DarkMode in applyStyle)
    for (auto *w : m_themeCards)
        if (auto *c = qobject_cast<OptionCard*>(w)) c->setSelected(c->isSelected());
    for (auto *w : m_mediaCards)
        if (auto *c = qobject_cast<OptionCard*>(w)) c->setSelected(c->isSelected());
}

// ── Section helpers ───────────────────────────────────────────────────────────

QLabel *SettingsPanel::makeSectionTitle(QWidget *parent, QVBoxLayout *layout,
                                         const QString &text)
{
    auto *lbl = new QLabel(text, parent);
    lbl->setObjectName(QStringLiteral("SettingsSectionTitle"));
    layout->addWidget(lbl);
    return lbl;
}

void SettingsPanel::makeSectionDesc(QWidget *parent, QVBoxLayout *layout,
                                     const QString &text)
{
    auto *lbl = new QLabel(text, parent);
    lbl->setObjectName(QStringLiteral("SettingsSectionDesc"));
    layout->addWidget(lbl);
    layout->addSpacing(14);
}

void SettingsPanel::makeSeparator(QWidget *parent, QVBoxLayout *layout)
{
    layout->addSpacing(8);
    auto *sep = new QFrame(parent);
    sep->setObjectName(QStringLiteral("SettingsSeparator"));
    sep->setFrameShape(QFrame::HLine);
    layout->addWidget(sep);
    layout->addSpacing(20);
}

// ── Sections ──────────────────────────────────────────────────────────────────

void SettingsPanel::buildSection_Appearance(QWidget *parent, QVBoxLayout *layout)
{
    makeSectionTitle(parent, layout, tr("Appearance"));
    makeSectionDesc(parent, layout, tr("Choose how OpenPDF Studio is displayed."));

    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    row->setAlignment(Qt::AlignLeft);

    struct T { const char *icon, *id, *title, *desc; };
    const T themes[] = {
        { "monitor", "system", QT_TR_NOOP("System"), QT_TR_NOOP("Follows system settings") },
        { "sun",     "light",  QT_TR_NOOP("Light"),  QT_TR_NOOP("Light user interface") },
        { "moon",    "dark",   QT_TR_NOOP("Dark"),   QT_TR_NOOP("Dark user interface") },
    };
    for (const auto &t : themes)
        row->addWidget(buildOptionCard(
            QLatin1String(t.icon), tr(t.title), tr(t.desc),
            QLatin1String(t.id), m_themeCards, m_themeIds, true));
    row->addStretch(1);
    layout->addLayout(row);
}

void SettingsPanel::buildSection_Language(QWidget *parent, QVBoxLayout *layout)
{
    m_langAnchor = makeSectionTitle(parent, layout, tr("Language"));
    makeSectionDesc(parent, layout, tr("Choose your preferred language."));

    m_langCombo = new QComboBox(parent);
    m_langCombo->setObjectName(QStringLiteral("SettingsLangCombo"));
    m_langCombo->setFixedWidth(240);
    m_langCombo->addItem(tr("English"), QStringLiteral("en"));
    m_langCombo->addItem(tr("German"),  QStringLiteral("de"));
    {
        const int idx = m_langCombo->findData(m_pendingLang);
        if (idx >= 0) m_langCombo->setCurrentIndex(idx);
    }
    connect(m_langCombo, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_pendingLang = m_langCombo->itemData(i).toString();
    });
    layout->addWidget(m_langCombo);
}

void SettingsPanel::buildSection_MediaPlayback(QWidget *parent, QVBoxLayout *layout)
{
    m_mediaAnchor = makeSectionTitle(parent, layout, tr("Media Playback"));
    makeSectionDesc(parent, layout, tr("Choose how media is played in OpenPDF Studio."));

    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    row->setAlignment(Qt::AlignLeft);

    struct M { const char *icon, *id, *title, *desc; };
    const M media[] = {
        { "play-circle", "default", QT_TR_NOOP("Default"),   QT_TR_NOOP("Use system player") },
        { "folder-open", "custom",  QT_TR_NOOP("Custom..."), QT_TR_NOOP("Custom media player") },
    };
    for (const auto &m : media)
        row->addWidget(buildOptionCard(
            QLatin1String(m.icon), tr(m.title), tr(m.desc),
            QLatin1String(m.id), m_mediaCards, m_mediaIds, false));
    row->addStretch(1);
    layout->addLayout(row);
}

void SettingsPanel::buildSection_Advanced(QWidget *parent, QVBoxLayout *layout)
{
    m_advAnchor = makeSectionTitle(parent, layout, tr("Advanced"));
    makeSectionDesc(parent, layout, tr("Advanced settings for OpenPDF Studio."));

    auto *resetAllBtn = new QPushButton(tr("Reset All Settings to Defaults"), parent);
    resetAllBtn->setFixedWidth(280);
    resetAllBtn->setStyleSheet(Theme::DarkMode
        ? QStringLiteral(
            "QPushButton { background:#3D1A1A; color:#F87171; border:1px solid #7F1D1D;"
            " border-radius:6px; font-size:13px; padding:8px 16px; }"
            "QPushButton:hover { background:#4D2020; }")
        : QStringLiteral(
            "QPushButton { background:#FEF2F2; color:#DC2626; border:1px solid #FCA5A5;"
            " border-radius:6px; font-size:13px; padding:8px 16px; }"
            "QPushButton:hover { background:#FEE2E2; }"));
    connect(resetAllBtn, &QPushButton::clicked, this, [this]() {
        m_pendingTheme = QStringLiteral("system");
        m_pendingLang  = QStringLiteral("en");
        selectCardGroup(m_pendingTheme, m_themeCards, m_themeIds);
        Q_EMIT themeChangeRequested(m_pendingTheme);
        if (m_langCombo) {
            const int idx = m_langCombo->findData(m_pendingLang);
            if (idx >= 0) m_langCombo->setCurrentIndex(idx);
        }
    });
    layout->addWidget(resetAllBtn);
}

// ── Option card ───────────────────────────────────────────────────────────────

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

// ── About page ────────────────────────────────────────────────────────────────

QWidget *SettingsPanel::buildAboutPage()
{
    auto *w = new QWidget(this);
    w->setObjectName(QStringLiteral("SettingsAboutPage"));
    auto *vbox = new QVBoxLayout(w);
    vbox->setContentsMargins(32, 32, 32, 32);
    vbox->setSpacing(10);
    vbox->setAlignment(Qt::AlignCenter);

    auto *logo = new QLabel(QStringLiteral("O"), w);
    logo->setObjectName(QStringLiteral("AppLogo"));
    logo->setFixedSize(52, 52);
    logo->setAlignment(Qt::AlignCenter);
    vbox->addWidget(logo, 0, Qt::AlignHCenter);

    vbox->addSpacing(4);

    auto *title = new QLabel(QStringLiteral("OpenPDF Studio"), w);
    title->setObjectName(QStringLiteral("SettingsSectionTitle"));
    title->setAlignment(Qt::AlignCenter);
    vbox->addWidget(title, 0, Qt::AlignHCenter);

    auto *ver = new QLabel(QStringLiteral("Version %1").arg(QLatin1String(APP_VERSION)), w);
    ver->setObjectName(QStringLiteral("SettingsSectionDesc"));
    ver->setAlignment(Qt::AlignCenter);
    vbox->addWidget(ver, 0, Qt::AlignHCenter);

    vbox->addSpacing(16);

    auto *desc = new QLabel(tr("A modern, open-source PDF editor built with Qt."), w);
    desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    desc->setAlignment(Qt::AlignCenter);
    vbox->addWidget(desc, 0, Qt::AlignHCenter);

    return w;
}

// ── Apply & misc ──────────────────────────────────────────────────────────────

void SettingsPanel::applyAndClose()
{
    m_settings->setTheme(m_pendingTheme);
    m_settings->setLanguage(m_pendingLang);
    m_settings->sync();

    if (m_pendingLang != m_originalLang)
        Q_EMIT languageChangeRequested(m_pendingLang);

    accept();
}

void SettingsPanel::retranslateUi()
{
    setWindowTitle(tr("Settings - OpenPDF Studio"));
}

void SettingsPanel::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    else if (e->type() == QEvent::StyleChange)
        refreshThemeColors();
    QDialog::changeEvent(e);
}
