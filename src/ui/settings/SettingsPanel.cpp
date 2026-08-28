#include "ui/settings/SettingsPanel.hpp"
#include "app/AppSettings.hpp"
#include "app/AppConfig.hpp"
#include "app/UpdateChecker.hpp"
#include "drm/LicenseStore.hpp"
#include "drm/LicensePage.hpp"
#include "ui/theme/Theme.hpp"
#include "ui/settings/LangRow.hpp"
#include "ui/settings/OptionCard.hpp"
#include "ui/settings/SettingCheckBox.hpp"
#include "ui/settings/ShortcutRow.hpp"
#include "ui/settings/ToggleSwitch.hpp"

#include <QVBoxLayout>
#include <QFileDialog>
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
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QSpinBox>







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
    setFixedSize(900, 660);

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
    m_pages->addWidget(buildShortcutsPage());   // 3
    m_pages->addWidget(buildZoomPage());        // 4
    m_pages->addWidget(buildAdvancedPage());    // 5
    // License: business installations only — see buildNav().
    if (License::isBusinessInstall()) {
        m_licensePage = new LicensePage(this);
        m_pages->addWidget(m_licensePage);
    }
    m_pages->addWidget(buildAboutPage());       // last

    // Nav entry i shows page i — including the optional license entry, which is
    // why both lists are built from the same condition.
    Q_ASSERT(m_pages->count() == m_navItems.size());

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
    QList<Item> items = {
        { "monitor",     QT_TR_NOOP("Appearance")     },
        { "globe",       QT_TR_NOOP("Language")       },
        { "play-circle", QT_TR_NOOP("Media Playback") },
        { "keyboard",    QT_TR_NOOP("Shortcuts")      },
        { "zoom-in",     QT_TR_NOOP("Zoom")           },
        { "sliders",     QT_TR_NOOP("Advanced")       },
    };
    // A personal installation has nothing to do with license keys and does not
    // get the page — the entry only exists where a key can actually be needed.
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
}

// ── Page builders ─────────────────────────────────────────────────────────────

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
    // Handed over before it is filled — the scroll area follows the layout as
    // rows are added, so the caller does not have to close the loop itself.
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

    auto *desc = new QLabel(tr("Choose how video embedded in a PDF is played."), page);
    desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    vbox->addWidget(desc);
    vbox->addSpacing(24);

    m_pendingMedia = m_settings ? m_settings->mediaPlayback() : QStringLiteral("inapp");

    auto *row = new QHBoxLayout;
    row->setSpacing(14);
    row->setAlignment(Qt::AlignLeft);

    struct M { const char *icon, *id, *title, *desc; };
    const M media[] = {
        { "play-circle", "inapp",  QT_TR_NOOP("In OpenPDF Studio"),
          QT_TR_NOOP("Plays on the page") },
        { "monitor",     "system", QT_TR_NOOP("System player"),
          QT_TR_NOOP("Opens in your video player") },
        { "folder-open", "custom", QT_TR_NOOP("Custom command"),
          QT_TR_NOOP("Run a player of your choice") },
    };
    for (const auto &m : media)
        row->addWidget(buildOptionCard(
            QLatin1String(m.icon), tr(m.title), tr(m.desc),
            QLatin1String(m.id), m_mediaCards, m_mediaIds, false));
    row->addStretch(1);
    vbox->addLayout(row);

#ifndef HAVE_QT_MULTIMEDIA
    // This build has no built-in player and falls back to the system one;
    // say so rather than offer a card that does nothing.
    vbox->addSpacing(12);
    auto *noPlayer = new QLabel(
        tr("This build has no built-in player. "
           "\"In OpenPDF Studio\" falls back to the system player."), page);
    noPlayer->setObjectName(QStringLiteral("SettingsSectionDesc"));
    noPlayer->setWordWrap(true);
    vbox->addWidget(noPlayer);
#endif

    // ── Custom command ───────────────────────────────────────────────────────
    vbox->addSpacing(20);
    m_customPlayerRow = new QWidget(page);
    auto *cmdLayout = new QHBoxLayout(m_customPlayerRow);
    cmdLayout->setContentsMargins(0, 0, 0, 0);
    cmdLayout->setSpacing(8);

    auto *cmdLabel = new QLabel(tr("Command:"), m_customPlayerRow);
    cmdLayout->addWidget(cmdLabel);

    m_customPlayerEdit = new QLineEdit(m_customPlayerRow);
    m_customPlayerEdit->setPlaceholderText(QStringLiteral("mpv --fs %1"));
    if (m_settings) m_customPlayerEdit->setText(m_settings->customPlayerCommand());
    m_customPlayerEdit->setToolTip(
        tr("%1 is replaced with the video file. Without it the file is appended."));
    cmdLayout->addWidget(m_customPlayerEdit, 1);

    auto *browse = new QPushButton(tr("Browse..."), m_customPlayerRow);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString exe = QFileDialog::getOpenFileName(this, tr("Choose a media player"));
        if (!exe.isEmpty())
            m_customPlayerEdit->setText(QStringLiteral("%1 %2").arg(exe, QStringLiteral("%1")));
    });
    cmdLayout->addWidget(browse);
    vbox->addWidget(m_customPlayerRow);

    m_customPlayerRow->setEnabled(m_pendingMedia == QLatin1String("custom"));

    vbox->addStretch(1);

    // The stored choice wins on open; buildOptionCard would otherwise mark
    // "default", which no longer exists here.
    selectCardGroup(m_pendingMedia, m_mediaCards, m_mediaIds);
    return page;
}

QWidget *SettingsPanel::buildShortcutsPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Header
    {
        auto *hdr = new QWidget(page);
        hdr->setObjectName(QStringLiteral("SettingsScrollContent"));
        auto *vl = new QVBoxLayout(hdr);
        vl->setContentsMargins(32, 28, 32, 16);
        vl->setSpacing(6);
        auto *title = new QLabel(tr("Keyboard Shortcuts"), hdr);
        title->setObjectName(QStringLiteral("SettingsSectionTitle"));
        vl->addWidget(title);
        auto *desc = new QLabel(tr("Configure keyboard shortcuts for frequent actions."), hdr);
        desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
        vl->addWidget(desc);
        outer->addWidget(hdr);
    }

    // Search row
    {
        auto *row = new QWidget(page);
        row->setObjectName(QStringLiteral("SettingsScrollContent"));
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(32, 0, 32, 12);
        hl->setSpacing(12);
        auto *searchEdit = new QLineEdit(row);
        searchEdit->setObjectName(QStringLiteral("ShortcutSearch"));
        searchEdit->setPlaceholderText(tr("Search action"));
        searchEdit->setFixedHeight(34);
        hl->addWidget(searchEdit, 1);
        auto *globalChk = new QCheckBox(tr("Show only global default shortcuts"), row);
        globalChk->setObjectName(QStringLiteral("ShortcutGlobalChk"));
        hl->addWidget(globalChk);
        outer->addWidget(row);

        // Search filter wired after m_shortcutRows is populated below
        connect(searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
            for (ShortcutRow *r : m_shortcutRows)
                r->setVisible(r->matchesFilter(text));
        });
    }

    // Column headers
    {
        auto *hdr = new QWidget(page);
        hdr->setObjectName(QStringLiteral("ShortcutTableHeader"));
        hdr->setFixedHeight(30);
        auto *hl = new QHBoxLayout(hdr);
        hl->setContentsMargins(16, 0, 16, 0);
        hl->setSpacing(8);
        auto *colAction = new QLabel(tr("Action"), hdr);
        colAction->setObjectName(QStringLiteral("ShortcutColHeader"));
        colAction->setFixedWidth(260);
        hl->addWidget(colAction);
        auto *colKey = new QLabel(tr("Key Combination"), hdr);
        colKey->setObjectName(QStringLiteral("ShortcutColHeader"));
        hl->addWidget(colKey, 1);
        outer->addWidget(hdr);
    }

    // Separator
    {
        auto *sep = new QFrame(page);
        sep->setObjectName(QStringLiteral("SettingsSeparator"));
        sep->setFrameShape(QFrame::HLine);
        outer->addWidget(sep);
    }

    // Scrollable list
    {
        auto *scroll = new QScrollArea(page);
        scroll->setObjectName(QStringLiteral("SettingsScroll"));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidgetResizable(true);

        auto *listW = new QWidget;
        listW->setObjectName(QStringLiteral("SettingsScrollContent"));
        auto *vl = new QVBoxLayout(listW);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);

        struct Def { const char *label; const char *key; const char *seq; };
        static const Def kDefs[] = {
            { QT_TR_NOOP("Save"),          "save",     "Ctrl+S"       },
            { QT_TR_NOOP("Save As"),       "saveas",   "Ctrl+Shift+S" },
            { QT_TR_NOOP("Print"),         "print",    "Ctrl+P"       },
            { QT_TR_NOOP("Open"),          "open",     "Ctrl+O"       },
            { QT_TR_NOOP("Undo"),          "undo",     "Ctrl+Z"       },
            { QT_TR_NOOP("Redo"),          "redo",     "Ctrl+Y"       },
            { QT_TR_NOOP("Find"),          "find",     "Ctrl+F"       },
            { QT_TR_NOOP("Text Tool"),     "texttool", "T"            },
            { QT_TR_NOOP("Notes"),         "comment",  "N"            },
            { QT_TR_NOOP("Zoom In"),       "zoomin",       "Ctrl++"       },
            { QT_TR_NOOP("Zoom Out"),      "zoomout",      "Ctrl+-"       },
            { QT_TR_NOOP("Presentation"),  "presentation", "F5"           },
        };

        m_shortcutRows.clear();
        m_shortcutKeys.clear();
        bool first = true;
        for (const auto &def : kDefs) {
            if (!first) {
                auto *sep = new QFrame(listW);
                sep->setObjectName(QStringLiteral("SettingsSeparator"));
                sep->setFrameShape(QFrame::HLine);
                vl->addWidget(sep);
            }
            first = false;

            const QKeySequence appDefault = QKeySequence::fromString(
                QLatin1String(def.seq), QKeySequence::PortableText);
            const QKeySequence saved = m_settings->shortcut(
                QLatin1String(def.key), appDefault);

            auto *srow = new ShortcutRow(tr(def.label), appDefault, listW);
            srow->setCurrentSequence(saved);

            m_shortcutRows.append(srow);
            m_shortcutKeys.append(QLatin1String(def.key));

            connect(srow, &ShortcutRow::editStarted, this, [this, srow]() {
                for (ShortcutRow *r : m_shortcutRows)
                    if (r != srow) r->cancelIfEditing();
            });
            vl->addWidget(srow);
        }
        vl->addStretch(1);
        scroll->setWidget(listW);
        outer->addWidget(scroll, 1);
    }

    // Info bar
    {
        auto *bar = new QFrame(page);
        bar->setObjectName(QStringLiteral("ShortcutInfoBar"));
        bar->setFrameShape(QFrame::NoFrame);
        bar->setFixedHeight(42);
        auto *hl = new QHBoxLayout(bar);
        hl->setContentsMargins(20, 0, 20, 0);
        hl->setSpacing(8);
        auto *icon = new QLabel(QStringLiteral("ℹ"), bar);
        icon->setObjectName(QStringLiteral("ShortcutInfoIcon"));
        hl->addWidget(icon);
        auto *text = new QLabel(tr("Double-click on a key combination to change it."), bar);
        text->setObjectName(QStringLiteral("ShortcutInfoText"));
        hl->addWidget(text, 1);
        outer->addWidget(bar);
    }

    return page;
}

QWidget *SettingsPanel::buildZoomPage()
{
    QWidget *page = nullptr;
    QVBoxLayout *vl = buildScrollPage(page, tr("Zoom"), tr("Configure how zoom works with the mouse wheel."));

    // Helper: single-row card with a toggle switch
    const auto makeToggleCard = [&](const QString &label, bool checked,
                                     QAbstractButton **out) -> QFrame * {
        auto *card = new QFrame(vl->parentWidget());
        card->setObjectName(QStringLiteral("ZoomCard"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto *hl = new QHBoxLayout(card);
        hl->setContentsMargins(16, 14, 16, 14);
        hl->setSpacing(12);
        auto *lbl = new QLabel(label, card);
        lbl->setObjectName(QStringLiteral("ZoomRowLabel"));
        hl->addWidget(lbl, 1);
        auto *toggle = new ToggleSwitch(card);
        toggle->setChecked(checked);
        hl->addWidget(toggle);
        if (out) *out = toggle;
        return card;
    };

    // ── Card 1: Mouse wheel zoom step ────────────────────────────────────────
    {
        auto *card = new QFrame(vl->parentWidget());
        card->setObjectName(QStringLiteral("ZoomCard"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(16, 16, 16, 16);
        cl->setSpacing(10);

        auto *cardTitle = new QLabel(tr("Mouse Wheel Zoom Step"), card);
        cardTitle->setObjectName(QStringLiteral("ZoomCardTitle"));
        cl->addWidget(cardTitle);

        auto *stepRow = new QHBoxLayout;
        stepRow->setSpacing(12);
        auto *stepLbl = new QLabel(tr("Zoom step with Ctrl + Mouse Wheel"), card);
        stepLbl->setObjectName(QStringLiteral("ZoomRowLabel"));
        stepRow->addWidget(stepLbl, 1);
        m_zoomStepSpin = new QSpinBox(card);
        m_zoomStepSpin->setObjectName(QStringLiteral("ZoomStepSpin"));
        m_zoomStepSpin->setRange(1, 50);
        m_zoomStepSpin->setValue(m_settings->zoomStep());
        m_zoomStepSpin->setSuffix(QStringLiteral(" %"));
        m_zoomStepSpin->setFixedWidth(88);
        stepRow->addWidget(m_zoomStepSpin);
        cl->addLayout(stepRow);

        auto *stepDesc = new QLabel(tr("Sets how much is zoomed per mouse wheel movement."), card);
        stepDesc->setObjectName(QStringLiteral("ZoomRowDesc"));
        cl->addWidget(stepDesc);

        auto *exBox = new QFrame(card);
        exBox->setObjectName(QStringLiteral("ZoomExampleBox"));
        exBox->setAttribute(Qt::WA_StyledBackground, true);
        auto *exRow = new QHBoxLayout(exBox);
        exRow->setContentsMargins(10, 8, 10, 8);
        exRow->setSpacing(6);
        auto *exIcon = new QLabel(QStringLiteral("↗"), exBox);
        exIcon->setObjectName(QStringLiteral("ZoomExampleIcon"));
        exRow->addWidget(exIcon);
        m_zoomExampleLabel = new QLabel(exBox);
        m_zoomExampleLabel->setObjectName(QStringLiteral("ZoomExampleText"));
        exRow->addWidget(m_zoomExampleLabel, 1);
        cl->addWidget(exBox);

        vl->addWidget(card);
    }

    // ── Card 2: Ctrl+Wheel toggle ─────────────────────────────────────────────
    vl->addWidget(makeToggleCard(
        tr("Zoom with Ctrl + Mouse Wheel"),
        m_settings->ctrlWheelZoom(),
        &m_ctrlWheelToggle));

    // ── Card 3: Zoom to pointer toggle ────────────────────────────────────────
    vl->addWidget(makeToggleCard(
        tr("Zoom to Mouse Pointer"),
        m_settings->zoomToPointer(),
        &m_zoomPtrToggle));

    // ── Card 4: Wheel action combo ────────────────────────────────────────────
    {
        auto *card = new QFrame(vl->parentWidget());
        card->setObjectName(QStringLiteral("ZoomCard"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto *hl = new QHBoxLayout(card);
        hl->setContentsMargins(16, 14, 16, 14);
        hl->setSpacing(12);
        auto *lbl = new QLabel(tr("Action without Ctrl"), card);
        lbl->setObjectName(QStringLiteral("ZoomRowLabel"));
        hl->addWidget(lbl, 1);
        m_wheelActionCombo = new QComboBox(card);
        m_wheelActionCombo->setObjectName(QStringLiteral("ZoomActionCombo"));
        m_wheelActionCombo->addItem(tr("Scroll"), QStringLiteral("scroll"));
        m_wheelActionCombo->addItem(tr("Zoom"),   QStringLiteral("zoom"));
        const QString act = m_settings->wheelAction();
        for (int i = 0; i < m_wheelActionCombo->count(); ++i) {
            if (m_wheelActionCombo->itemData(i).toString() == act) {
                m_wheelActionCombo->setCurrentIndex(i);
                break;
            }
        }
        hl->addWidget(m_wheelActionCombo);
        vl->addWidget(card);
    }

    finishScrollPage(vl);

    // Update example label whenever the step changes
    const auto updateExample = [this]() {
        m_zoomExampleLabel->setText(
            tr("Example: 100 % → %1 %").arg(100 + m_zoomStepSpin->value()));
    };
    updateExample();
    connect(m_zoomStepSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, updateExample);

    return page;
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

void SettingsPanel::buildAdvancedUpdates(QVBoxLayout *vl)
{
    QVBoxLayout *cl = addSettingsCard(vl, tr("Updates"));
    m_autoUpdateCheck = addSettingsCheck(cl, tr("Check for automatic updates"),
        tr("Automatically check for updates in the background and notify you "
           "when a new version is available."),
        m_settings->autoUpdateCheck());

    m_updateIntervalCombo = addSettingsCombo(cl, tr("Check interval:"));
    m_updateIntervalCombo->addItem(tr("On every start"), QStringLiteral("startup"));
    m_updateIntervalCombo->addItem(tr("Daily"),          QStringLiteral("daily"));
    m_updateIntervalCombo->addItem(tr("Weekly"),         QStringLiteral("weekly"));
    m_updateIntervalCombo->addItem(tr("Monthly"),        QStringLiteral("monthly"));
    selectComboData(m_updateIntervalCombo, m_settings->updateInterval());

    // An interval means nothing while the check is off.
    m_updateIntervalCombo->setEnabled(m_autoUpdateCheck->isChecked());
    connect(m_autoUpdateCheck, &QAbstractButton::toggled,
            m_updateIntervalCombo, &QWidget::setEnabled);

    // The switch above only decides when the program asks by itself. This row
    // asks now, and says what came back - without it the whole card is a
    // setting with nothing behind it.
    QWidget *card = cl->parentWidget();
    auto *row = new QHBoxLayout;
    row->setContentsMargins(SettingCheckBox::kBox + SettingCheckBox::kSpacing,
                            10, 0, 0);
    row->setSpacing(12);

    m_updateCheckBtn = new QPushButton(tr("Check now"), card);
    m_updateCheckBtn->setObjectName(QStringLiteral("SettingsActionBtn"));
    m_updateCheckBtn->setCursor(Qt::PointingHandCursor);
    m_updateCheckBtn->setFixedHeight(28);
    row->addWidget(m_updateCheckBtn, 0, Qt::AlignVCenter);

    m_updateStatus = new QLabel(card);
    m_updateStatus->setObjectName(QStringLiteral("SettingsRowDesc"));
    m_updateStatus->setWordWrap(true);
    m_updateStatus->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_updateStatus->setOpenExternalLinks(true);
    const QDateTime last = m_settings->lastUpdateCheck();
    m_updateStatus->setText(last.isValid()
        ? tr("Last checked %1").arg(QLocale().toString(last.toLocalTime(),
                                                       QLocale::ShortFormat))
        : tr("Not checked yet"));
    row->addWidget(m_updateStatus, 1);

    cl->addLayout(row);

    m_updateChecker = new UpdateChecker(m_settings, this);
    connect(m_updateCheckBtn, &QPushButton::clicked, this, [this]() {
        m_updateCheckBtn->setEnabled(false);
        m_updateStatus->setTextFormat(Qt::PlainText);
        m_updateStatus->setText(tr("Checking…"));
        m_updateChecker->checkNow();
    });
    connect(m_updateChecker, &UpdateChecker::finished,
            this, &SettingsPanel::showUpdateResult);
}

void SettingsPanel::showUpdateResult(const UpdateCheckResult &result)
{
    if (!m_updateStatus || !m_updateCheckBtn)
        return;
    m_updateCheckBtn->setEnabled(true);

    if (!result.ok) {
        m_updateStatus->setTextFormat(Qt::PlainText);
        m_updateStatus->setText(tr("Check failed: %1").arg(result.error));
        return;
    }
    if (!result.updateAvailable) {
        m_updateStatus->setTextFormat(Qt::PlainText);
        m_updateStatus->setText(tr("Version %1 is up to date.").arg(result.current));
        return;
    }
    // The program does not install anything, so the sentence links to the
    // download page - the tag only said that there is something newer. The link
    // colour is set here because a stylesheet does not reach into rich text;
    // the primary blue is too dark to read on the dark card.
    m_updateStatus->setTextFormat(Qt::RichText);
    m_updateStatus->setText(
        tr("Version %1 is available. <a href=\"%2\" style=\"color:%3;\">Download</a>")
            .arg(result.latest,
                 QString::fromLatin1(UpdateChecker::downloadPageUrl().toEncoded()),
                 Theme::DarkMode ? QStringLiteral("#60A5FA")
                                 : Theme::Primary.name()));
}

void SettingsPanel::buildAdvancedInterface(QVBoxLayout *vl)
{
    QVBoxLayout *cl = addSettingsCard(vl, tr("Interface"));
    m_preserveLayoutCheck = addSettingsCheck(cl, tr("Preserve panel layout"),
        tr("Restore expanded and collapsed panels after restart."),
        m_settings->preservePanelLayout());
}

void SettingsPanel::buildAdvancedPerformance(QVBoxLayout *vl)
{
    QVBoxLayout *cl = addSettingsCard(vl, tr("Performance"));
    m_hwAccelCheck = addSettingsCheck(cl, tr("Use hardware acceleration when available"),
        tr("Improves performance for rendering and media playback."),
        m_settings->hardwareAcceleration());
    cl->addSpacing(6);
    m_limitMemoryCheck = addSettingsCheck(cl, tr("Limit memory usage"),
        tr("Helps improve stability on systems with limited memory."),
        m_settings->limitMemoryUsage());
}

void SettingsPanel::buildAdvancedDiagnostics(QVBoxLayout *vl)
{
    QVBoxLayout *cl = addSettingsCard(vl, tr("Diagnostics"));
    m_debugLogCheck = addSettingsCheck(cl, tr("Enable debug logging"),
        tr("Save detailed logs for troubleshooting purposes."),
        m_settings->debugLogging());

    m_logLevelCombo = addSettingsCombo(cl, tr("Log level:"));
    m_logLevelCombo->addItem(tr("Error"),   QStringLiteral("error"));
    m_logLevelCombo->addItem(tr("Warning"), QStringLiteral("warning"));
    m_logLevelCombo->addItem(tr("Info"),    QStringLiteral("info"));
    m_logLevelCombo->addItem(tr("Debug"),   QStringLiteral("debug"));
    selectComboData(m_logLevelCombo, m_settings->logLevel());

    m_logLevelCombo->setEnabled(m_debugLogCheck->isChecked());
    connect(m_debugLogCheck, &QAbstractButton::toggled,
            m_logLevelCombo, &QWidget::setEnabled);
}

void SettingsPanel::buildAdvancedReset(QVBoxLayout *vl)
{
    QVBoxLayout *cl = addSettingsCard(vl, tr("Reset"));
    auto *resetAllBtn = new QPushButton(tr("Reset All Settings to Defaults"),
                                        cl->parentWidget());
    resetAllBtn->setFixedWidth(280);
    resetAllBtn->setCursor(Qt::PointingHandCursor);
    resetAllBtn->setStyleSheet(Theme::DarkMode
        ? QStringLiteral(
            "QPushButton { background:#3D1A1A; color:#F87171; border:1px solid #7F1D1D;"
            " border-radius:6px; font-size:13px; padding:8px 16px; }"
            "QPushButton:hover { background:#4D2020; }")
        : QStringLiteral(
            "QPushButton { background:#FEF2F2; color:#DC2626; border:1px solid #FCA5A5;"
            " border-radius:6px; font-size:13px; padding:8px 16px; }"
            "QPushButton:hover { background:#FEE2E2; }"));
    // Every control the dialog owns goes back to its default here; nothing is
    // written yet, so Cancel still leaves the stored settings untouched.
    connect(resetAllBtn, &QPushButton::clicked, this, [this]() {
        m_pendingTheme = QStringLiteral("system");
        selectCardGroup(m_pendingTheme, m_themeCards, m_themeIds);
        Q_EMIT themeChangeRequested(m_pendingTheme);
        m_pendingLang = QStringLiteral("en");
        selectLangCode(m_pendingLang);
        if (m_autoUpdateCheck)     m_autoUpdateCheck->setChecked(true);
        if (m_updateIntervalCombo) selectComboData(m_updateIntervalCombo,
                                              QStringLiteral("daily"));
        if (m_preserveLayoutCheck) m_preserveLayoutCheck->setChecked(true);
        if (m_hwAccelCheck)        m_hwAccelCheck->setChecked(false);
        if (m_limitMemoryCheck)    m_limitMemoryCheck->setChecked(false);
        if (m_debugLogCheck)       m_debugLogCheck->setChecked(false);
        if (m_logLevelCombo)       selectComboData(m_logLevelCombo,
                                              QStringLiteral("info"));
    });
    cl->addWidget(resetAllBtn, 0, Qt::AlignLeft);
}

QWidget *SettingsPanel::buildAdvancedPage()
{
    QWidget *page = nullptr;
    QVBoxLayout *vl = buildScrollPage(page, tr("Advanced"), tr("Advanced settings for OpenPDF Studio."));


    buildAdvancedUpdates(vl);
    buildAdvancedInterface(vl);
    buildAdvancedPerformance(vl);
    buildAdvancedDiagnostics(vl);
    buildAdvancedReset(vl);

    finishScrollPage(vl);
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
    vbox->addSpacing(16);

    // Wo die Einstellungen liegen, ist eine Support-Frage — hier steht die
    // Antwort, markierbar, statt in einer Anleitung, die niemand liest.
    auto *cfg = new QLabel(AppConfig::isPortable()
        ? tr("Configuration (portable): %1").arg(AppConfig::path())
        : tr("Configuration: %1").arg(AppConfig::path()), page);
    cfg->setObjectName(QStringLiteral("SettingsSectionDesc"));
    cfg->setAlignment(Qt::AlignCenter);
    cfg->setWordWrap(true);
    cfg->setTextInteractionFlags(Qt::TextSelectableByMouse);
    vbox->addWidget(cfg, 0, Qt::AlignHCenter);
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
    if (!m_pendingMedia.isEmpty())
        m_settings->setMediaPlayback(m_pendingMedia);
    if (m_customPlayerEdit)
        m_settings->setCustomPlayerCommand(m_customPlayerEdit->text().trimmed());

    // Flush any open shortcut edit before saving
    for (ShortcutRow *r : m_shortcutRows) r->cancelIfEditing();

    // Persist all shortcuts and apply them immediately
    for (int i = 0; i < m_shortcutRows.size() && i < m_shortcutKeys.size(); ++i)
        m_settings->setShortcut(m_shortcutKeys[i], m_shortcutRows[i]->currentSequence());

    // Save zoom settings
    if (m_zoomStepSpin) {
        m_settings->setZoomStep(m_zoomStepSpin->value());
        m_settings->setCtrlWheelZoom(m_ctrlWheelToggle && m_ctrlWheelToggle->isChecked());
        m_settings->setZoomToPointer(m_zoomPtrToggle && m_zoomPtrToggle->isChecked());
        if (m_wheelActionCombo)
            m_settings->setWheelAction(m_wheelActionCombo->currentData().toString());
    }

    // Panel layout
    bool panelLayoutChanged = false;
    if (m_preserveLayoutCheck) {
        const bool preserve = m_preserveLayoutCheck->isChecked();
        panelLayoutChanged = (preserve != m_settings->preservePanelLayout());
        m_settings->setPreservePanelLayout(preserve);
    }

    // Advanced
    if (m_autoUpdateCheck)
        m_settings->setAutoUpdateCheck(m_autoUpdateCheck->isChecked());
    if (m_updateIntervalCombo)
        m_settings->setUpdateInterval(m_updateIntervalCombo->currentData().toString());
    if (m_hwAccelCheck)
        m_settings->setHardwareAcceleration(m_hwAccelCheck->isChecked());
    if (m_limitMemoryCheck)
        m_settings->setLimitMemoryUsage(m_limitMemoryCheck->isChecked());
    if (m_debugLogCheck)
        m_settings->setDebugLogging(m_debugLogCheck->isChecked());
    if (m_logLevelCombo)
        m_settings->setLogLevel(m_logLevelCombo->currentData().toString());

    m_settings->sync();

    if (m_pendingLang != m_originalLang)
        Q_EMIT languageChangeRequested(m_pendingLang);

    Q_EMIT shortcutsChanged();      // always reload shortcuts in MainWindow
    Q_EMIT zoomSettingsChanged();   // always reload zoom settings in MainWindow
    // Switching the option on captures the layout that is on screen right now,
    // so the very next start already restores what the user is looking at.
    if (panelLayoutChanged)
        Q_EMIT panelLayoutSettingChanged();

    accept();
}

void SettingsPanel::retranslateUi()
{
    setWindowTitle(tr("Settings - OpenPDF Studio"));

    for (const NavItem &ni : m_navItems)
        ni.textLabel->setText(tr(ni.labelKey));
}

void SettingsPanel::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    else if (e->type() == QEvent::StyleChange)
        refreshThemeColors();
    QDialog::changeEvent(e);
}
