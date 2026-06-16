#include "SettingsPanel.hpp"
#include "app/AppSettings.hpp"
#include "ui/theme/Theme.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
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

// ── LangRow ───────────────────────────────────────────────────────────────────

class LangRow : public QFrame
{
    Q_OBJECT
public:
    explicit LangRow(const QString &code, const QString &displayName,
                     QWidget *parent = nullptr)
        : QFrame(parent), m_code(code)
    {
        setFixedHeight(48);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, true);

        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(20, 0, 20, 0);
        row->setSpacing(12);

        m_nameLabel = new QLabel(displayName, this);
        m_nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(m_nameLabel, 1);

        m_checkLabel = new QLabel(QStringLiteral("✓"), this);
        m_checkLabel->setFixedWidth(24);
        m_checkLabel->setAlignment(Qt::AlignCenter);
        m_checkLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(m_checkLabel);

        applyStyle(false);
    }

    QString code() const { return m_code; }
    bool isSelected() const { return m_selected; }

    void setSelected(bool v)
    {
        m_selected = v;
        m_checkLabel->setVisible(v);
        applyStyle(v);
    }

Q_SIGNALS:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) Q_EMIT clicked();
    }

private:
    void applyStyle(bool sel)
    {
        const bool dk = Theme::DarkMode;
        const QString selBg = dk ? "#1E3A5F" : "#EFF6FF";
        const QString hovBg = dk ? "#3E3E3E" : "#F3F4F6";
        const QString txtSel = dk ? "#93C5FD" : "#1D4ED8";
        const QString txtNor = dk ? "#D8D8D8" : "#111827";
        const QString chkClr = dk ? "#93C5FD" : "#2563EB";

        setStyleSheet(sel
            ? QStringLiteral("LangRow { background:%1; border:none; }"
                              "LangRow:hover { background:%1; }").arg(selBg)
            : QStringLiteral("LangRow { background:transparent; border:none; }"
                              "LangRow:hover { background:%1; }").arg(hovBg));

        m_nameLabel->setStyleSheet(QStringLiteral(
            "font-size:14px; color:%1; font-weight:%2;")
            .arg(sel ? txtSel : txtNor, sel ? QLatin1String("600") : QLatin1String("400")));

        m_checkLabel->setStyleSheet(QStringLiteral(
            "font-size:14px; font-weight:700; color:%1;").arg(chkClr));
    }

    QLabel *m_nameLabel  { nullptr };
    QLabel *m_checkLabel { nullptr };
    QString m_code;
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

// ── UI skeleton ───────────────────────────────────────────────────────────────

void SettingsPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *contentRow = new QHBoxLayout;
    contentRow->setContentsMargins(0, 0, 0, 0);
    contentRow->setSpacing(0);

    // ── Sidebar ───────────────────────────────────────────────────────────
    auto *sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("SettingsSidebar"));
    sidebar->setFixedWidth(190);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(8, 12, 8, 12);
    sideLayout->setSpacing(2);
    buildNav(sidebar, sideLayout);
    contentRow->addWidget(sidebar);

    // ── Pages ─────────────────────────────────────────────────────────────
    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("SettingsPages"));
    m_pages->addWidget(buildAppearancePage());  // 0
    m_pages->addWidget(buildLanguagePage());    // 1
    m_pages->addWidget(buildMediaPage());       // 2
    m_pages->addWidget(buildAdvancedPage());    // 3
    m_pages->addWidget(buildAboutPage());       // 4

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
        selectCardGroup(m_pendingTheme, m_themeCards, m_themeIds);
        Q_EMIT themeChangeRequested(m_pendingTheme);

        m_pendingLang = QStringLiteral("en");
        selectLangCode(m_pendingLang);
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

    selectPage(0);
}

// ── Nav ───────────────────────────────────────────────────────────────────────

void SettingsPanel::buildNav(QWidget *, QVBoxLayout *layout)
{
    struct Item { const char *icon; const char *label; };
    const Item items[] = {
        { "monitor",     QT_TR_NOOP("Appearance")     },
        { "globe",       QT_TR_NOOP("Language")       },
        { "play-circle", QT_TR_NOOP("Media Playback") },
        { "sliders",     QT_TR_NOOP("Advanced")       },
    };

    for (int i = 0; i < 4; ++i) {
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

        m_navItems.append({ btn, iconLbl, textLbl, QLatin1String(items[i].icon) });

        const int navIdx = i;
        connect(btn, &QPushButton::clicked, this, [this, navIdx]() { selectPage(navIdx); });
        layout->addWidget(btn);
    }

    layout->addStretch(1);

    // About — pinned to bottom
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
    m_navItems.append({ aboutBtn, aicon, atext, QStringLiteral("info") });
    connect(aboutBtn, &QPushButton::clicked, this, [this]() { selectPage(4); });
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
}

// ── Page builders ─────────────────────────────────────────────────────────────

QWidget *SettingsPanel::buildAppearancePage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(32, 28, 32, 32);
    vbox->setSpacing(0);

    auto *title = new QLabel(tr("Appearance"), page);
    title->setObjectName(QStringLiteral("SettingsSectionTitle"));
    vbox->addWidget(title);
    vbox->addSpacing(6);

    auto *desc = new QLabel(tr("Choose how OpenPDF Studio is displayed."), page);
    desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    vbox->addWidget(desc);
    vbox->addSpacing(24);

    auto *row = new QHBoxLayout;
    row->setSpacing(14);
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
    vbox->addLayout(row);
    vbox->addStretch(1);
    return page;
}

QWidget *SettingsPanel::buildLanguagePage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(0, 28, 0, 0);
    vbox->setSpacing(0);

    auto *titleWrap = new QWidget(page);
    titleWrap->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *twl = new QVBoxLayout(titleWrap);
    twl->setContentsMargins(32, 0, 32, 0);
    twl->setSpacing(6);

    auto *title = new QLabel(tr("Language"), titleWrap);
    title->setObjectName(QStringLiteral("SettingsSectionTitle"));
    twl->addWidget(title);

    auto *desc = new QLabel(tr("Choose your preferred interface language."), titleWrap);
    desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    twl->addWidget(desc);

    vbox->addWidget(titleWrap);
    vbox->addSpacing(16);

    // ── Scrollable language list ──────────────────────────────────────────
    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("SettingsScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidgetResizable(true);

    auto *listWidget = new QWidget;
    listWidget->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *listLayout = new QVBoxLayout(listWidget);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);

    // English first, then separator, then alphabetical by display name
    struct L { const char *code; const char *display; };
    const L langs[] = {
        { "en", "English"    },
        { nullptr, nullptr   },  // separator
        { "de", "Deutsch"    },
        { "fr", "Français"   },
        { "es", "Español"    },
        { "it", "Italiano"   },
        { "pt", "Português"  },
        { "nl", "Nederlands" },
        { "pl", "Polski"     },
        { "ru", "Русский"    },
        { "zh", "中文"        },
        { "ja", "日本語"      },
        { "ko", "한국어"      },
    };

    for (const auto &l : langs) {
        if (!l.code) {
            // Separator between English and other languages
            auto *sep = new QFrame(listWidget);
            sep->setObjectName(QStringLiteral("SettingsSeparator"));
            sep->setFrameShape(QFrame::HLine);
            sep->setFixedHeight(1);
            listLayout->addWidget(sep);
        } else {
            addLangRow(listWidget, listLayout, QLatin1String(l.code),
                       QString::fromUtf8(l.display));
        }
    }

    listLayout->addStretch(1);
    scroll->setWidget(listWidget);
    vbox->addWidget(scroll, 1);
    return page;
}

QWidget *SettingsPanel::buildMediaPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(32, 28, 32, 32);
    vbox->setSpacing(0);

    auto *title = new QLabel(tr("Media Playback"), page);
    title->setObjectName(QStringLiteral("SettingsSectionTitle"));
    vbox->addWidget(title);
    vbox->addSpacing(6);

    auto *desc = new QLabel(tr("Choose how media is played in OpenPDF Studio."), page);
    desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    vbox->addWidget(desc);
    vbox->addSpacing(24);

    auto *row = new QHBoxLayout;
    row->setSpacing(14);
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
    vbox->addLayout(row);
    vbox->addStretch(1);
    return page;
}

QWidget *SettingsPanel::buildAdvancedPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(32, 28, 32, 32);
    vbox->setSpacing(0);

    auto *title = new QLabel(tr("Advanced"), page);
    title->setObjectName(QStringLiteral("SettingsSectionTitle"));
    vbox->addWidget(title);
    vbox->addSpacing(6);

    auto *desc = new QLabel(tr("Advanced settings for OpenPDF Studio."), page);
    desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    vbox->addWidget(desc);
    vbox->addSpacing(24);

    auto *resetAllBtn = new QPushButton(tr("Reset All Settings to Defaults"), page);
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
        selectCardGroup(m_pendingTheme, m_themeCards, m_themeIds);
        Q_EMIT themeChangeRequested(m_pendingTheme);
        m_pendingLang = QStringLiteral("en");
        selectLangCode(m_pendingLang);
    });
    vbox->addWidget(resetAllBtn);
    vbox->addStretch(1);
    return page;
}

QWidget *SettingsPanel::buildAboutPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(32, 32, 32, 32);
    vbox->setSpacing(10);
    vbox->setAlignment(Qt::AlignCenter);

    auto *logo = new QLabel(QStringLiteral("O"), page);
    logo->setObjectName(QStringLiteral("AppLogo"));
    logo->setFixedSize(56, 56);
    logo->setAlignment(Qt::AlignCenter);
    vbox->addWidget(logo, 0, Qt::AlignHCenter);
    vbox->addSpacing(8);

    auto *appName = new QLabel(QStringLiteral("OpenPDF Studio"), page);
    appName->setObjectName(QStringLiteral("SettingsSectionTitle"));
    appName->setAlignment(Qt::AlignCenter);
    vbox->addWidget(appName, 0, Qt::AlignHCenter);

    auto *ver = new QLabel(
        QStringLiteral("Version %1").arg(QLatin1String(APP_VERSION)), page);
    ver->setObjectName(QStringLiteral("SettingsSectionDesc"));
    ver->setAlignment(Qt::AlignCenter);
    vbox->addWidget(ver, 0, Qt::AlignHCenter);
    vbox->addSpacing(16);

    auto *tagline = new QLabel(
        tr("A modern, open-source PDF editor built with Qt."), page);
    tagline->setObjectName(QStringLiteral("SettingsSectionDesc"));
    tagline->setAlignment(Qt::AlignCenter);
    vbox->addWidget(tagline, 0, Qt::AlignHCenter);
    return page;
}

// ── Option cards ──────────────────────────────────────────────────────────────

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

// ── Language rows ─────────────────────────────────────────────────────────────

void SettingsPanel::addLangRow(QWidget *parent, QVBoxLayout *layout,
                                const QString &code, const QString &display)
{
    auto *row = new LangRow(code, display, parent);
    row->setSelected(code == m_pendingLang);
    m_langRows.append(row);
    m_langCodes.append(code);

    connect(row, &LangRow::clicked, this, [this, code]() {
        selectLangCode(code);
        m_pendingLang = code;
    });
    layout->addWidget(row);
}

void SettingsPanel::selectLangCode(const QString &code)
{
    for (int i = 0; i < m_langRows.size(); ++i)
        qobject_cast<LangRow *>(m_langRows[i])->setSelected(m_langCodes[i] == code);
}

// ── Misc ──────────────────────────────────────────────────────────────────────

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

    static const char *navKeys[] = {
        QT_TR_NOOP("Appearance"),
        QT_TR_NOOP("Language"),
        QT_TR_NOOP("Media Playback"),
        QT_TR_NOOP("Advanced"),
        QT_TR_NOOP("About"),
    };
    for (int i = 0; i < m_navItems.size() && i < 5; ++i)
        m_navItems[i].textLabel->setText(tr(navKeys[i]));
}

void SettingsPanel::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    else if (e->type() == QEvent::StyleChange)
        refreshThemeColors();
    QDialog::changeEvent(e);
}
