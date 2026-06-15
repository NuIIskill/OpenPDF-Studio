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
#include <QFrame>
#include <QEvent>
#include <QMouseEvent>
#include <QResizeEvent>

// ── ThemeCard ─────────────────────────────────────────────────────────────────

class ThemeCard : public QWidget
{
    Q_OBJECT
public:
    explicit ThemeCard(const QString &iconName, const QString &title,
                       const QString &desc, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(180, 115);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, true);

        auto *vbox = new QVBoxLayout(this);
        vbox->setContentsMargins(12, 14, 12, 10);
        vbox->setSpacing(6);
        vbox->setAlignment(Qt::AlignHCenter);

        m_iconLabel = new QLabel(this);
        m_iconLabel->setAlignment(Qt::AlignCenter);
        const QPixmap px = Theme::renderSvg(iconName, QColor("#6B7280"), 28);
        m_iconLabel->setPixmap(px);
        vbox->addWidget(m_iconLabel);

        m_titleLabel = new QLabel(title, this);
        m_titleLabel->setAlignment(Qt::AlignCenter);
        m_titleLabel->setStyleSheet(QStringLiteral("font-size:13px; font-weight:600;"));
        vbox->addWidget(m_titleLabel);

        m_descLabel = new QLabel(desc, this);
        m_descLabel->setAlignment(Qt::AlignCenter);
        m_descLabel->setStyleSheet(QStringLiteral("font-size:11px; color:#6B7280;"));
        m_descLabel->setWordWrap(true);
        vbox->addWidget(m_descLabel);

        // Checkmark badge top-right
        m_check = new QLabel(this);
        m_check->setFixedSize(22, 22);
        m_check->setAlignment(Qt::AlignCenter);
        m_check->setStyleSheet(QStringLiteral(
            "background:#3B82F6; border-radius:11px; color:white; font-size:13px; font-weight:700;"));
        m_check->setText(QStringLiteral("✓"));
        m_check->hide();

        setChecked(false);
    }

    bool isChecked() const { return m_checked; }

    void setChecked(bool v)
    {
        m_checked = v;
        m_check->setVisible(v);
        const QPixmap px = Theme::renderSvg(
            m_iconLabel->toolTip().isEmpty() ? QStringLiteral("settings") : m_iconLabel->toolTip(),
            v ? QColor("#3B82F6") : QColor("#6B7280"), 28);
        // Re-render icon color
        if (!px.isNull()) m_iconLabel->setPixmap(px);

        if (v) {
            setStyleSheet(QStringLiteral(
                "ThemeCard { border:2px solid #3B82F6; border-radius:8px; background:#EFF6FF; }"));
            m_titleLabel->setStyleSheet(
                QStringLiteral("font-size:13px; font-weight:600; color:#1D4ED8;"));
        } else {
            setStyleSheet(QStringLiteral(
                "ThemeCard { border:1px solid #E5E7EB; border-radius:8px; background:white; }"));
            m_titleLabel->setStyleSheet(
                QStringLiteral("font-size:13px; font-weight:600; color:#111827;"));
        }
    }

    void setIconName(const QString &name)
    {
        m_iconName = name;
        // store in toolTip for retrieval in setChecked
        m_iconLabel->setToolTip(name);
        const QPixmap px = Theme::renderSvg(name, m_checked ? QColor("#3B82F6") : QColor("#6B7280"), 28);
        if (!px.isNull()) m_iconLabel->setPixmap(px);
    }

Q_SIGNALS:
    void activated();

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) Q_EMIT activated();
        QWidget::mousePressEvent(e);
    }

    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        m_check->move(width() - 28, 6);
    }

private:
    QLabel *m_iconLabel  { nullptr };
    QLabel *m_titleLabel { nullptr };
    QLabel *m_descLabel  { nullptr };
    QLabel *m_check      { nullptr };
    QString m_iconName;
    bool    m_checked    { false };
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
    setMinimumSize(820, 580);
    buildUi();

    // Revert live-preview theme if user cancels
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

    // ── Content row ───────────────────────────────────────────────────────
    auto *contentRow = new QHBoxLayout;
    contentRow->setContentsMargins(0, 0, 0, 0);
    contentRow->setSpacing(0);

    // Left sidebar
    auto *sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("SettingsSidebar"));
    sidebar->setFixedWidth(190);
    sidebar->setStyleSheet(QStringLiteral(
        "QWidget#SettingsSidebar { background:#F9FAFB; border-right:1px solid #E5E7EB; }"));

    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(8, 16, 8, 16);
    sideLayout->setSpacing(2);

    struct NavItem { const char *icon; const char *label; int page; };
    const NavItem navItems[] = {
        { "settings",    QT_TR_NOOP("General"),        0 },
        { "monitor",     QT_TR_NOOP("Appearance"),     1 },
        { "globe",       QT_TR_NOOP("Language"),       2 },
        { "play-circle", QT_TR_NOOP("Media Playback"), 3 },
        { "sliders",     QT_TR_NOOP("Advanced"),       4 },
    };

    for (const auto &item : navItems)
        sideLayout->addWidget(buildNavItem(
            QLatin1String(item.icon), tr(item.label), item.page));

    sideLayout->addStretch(1);
    sideLayout->addWidget(buildNavItem(QStringLiteral("info"),
                                       tr("About OpenPDF Studio"), 5));

    contentRow->addWidget(sidebar);

    // Right stacked pages
    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildGeneralPage());                            // 0 General
    m_pages->addWidget(buildPlaceholderPage(tr("Appearance")));       // 1
    m_pages->addWidget(buildPlaceholderPage(tr("Language")));         // 2
    m_pages->addWidget(buildPlaceholderPage(tr("Media Playback")));   // 3
    m_pages->addWidget(buildPlaceholderPage(tr("Advanced")));         // 4
    m_pages->addWidget(buildAboutPage());                              // 5
    contentRow->addWidget(m_pages, 1);

    root->addLayout(contentRow, 1);

    // ── Bottom bar ─────────────────────────────────────────────────────────
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color:#E5E7EB;"));
    root->addWidget(sep);

    auto *bottomBar = new QHBoxLayout;
    bottomBar->setContentsMargins(16, 12, 16, 12);
    bottomBar->setSpacing(8);

    auto *resetBtn = new QPushButton(tr("Reset"), this);
    resetBtn->setObjectName(QStringLiteral("SettingsResetBtn"));
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        m_pendingTheme = QStringLiteral("system");
        m_pendingLang  = QStringLiteral("en");
        selectThemeCard(m_pendingTheme);
        if (m_langCombo) {
            const int idx = m_langCombo->findData(m_pendingLang);
            if (idx >= 0) m_langCombo->setCurrentIndex(idx);
        }
    });
    bottomBar->addWidget(resetBtn);
    bottomBar->addStretch(1);

    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setObjectName(QStringLiteral("SettingsCancelBtn"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    bottomBar->addWidget(cancelBtn);

    auto *saveBtn = new QPushButton(tr("Save"), this);
    saveBtn->setObjectName(QStringLiteral("SettingsSaveBtn"));
    saveBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, &SettingsPanel::applyAndClose);
    bottomBar->addWidget(saveBtn);

    root->addLayout(bottomBar);
}

QWidget *SettingsPanel::buildNavItem(const QString &iconName,
                                      const QString &label, int pageIndex)
{
    auto *btn = new QPushButton(this);
    btn->setFixedHeight(40);
    btn->setFlat(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName(QStringLiteral("SettingsNavBtn"));

    auto *row = new QHBoxLayout(btn);
    row->setContentsMargins(10, 0, 10, 0);
    row->setSpacing(10);

    auto *icon = new QLabel(btn);
    icon->setFixedSize(18, 18);
    icon->setAlignment(Qt::AlignCenter);
    icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    const QPixmap px = Theme::renderSvg(iconName, QColor("#374151"), 18);
    if (!px.isNull()) icon->setPixmap(px);
    row->addWidget(icon);

    auto *text = new QLabel(label, btn);
    text->setAttribute(Qt::WA_TransparentForMouseEvents);
    text->setStyleSheet(QStringLiteral("font-size:13px; color:#111827;"));
    row->addWidget(text, 1);

    btn->setStyleSheet(QStringLiteral(
        "QPushButton#SettingsNavBtn { border-radius:6px; text-align:left; }"
        "QPushButton#SettingsNavBtn:hover { background:#E5E7EB; }"
        "QPushButton#SettingsNavBtn:checked { background:#DBEAFE; }"
        "QPushButton#SettingsNavBtn:checked QLabel { color:#1D4ED8; }"));

    connect(btn, &QPushButton::clicked, this, [this, pageIndex]() {
        m_pages->setCurrentIndex(pageIndex);
    });

    return btn;
}

QWidget *SettingsPanel::buildGeneralPage()
{
    auto *scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);

    auto *page = new QWidget;
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(32, 28, 32, 28);
    vbox->setSpacing(0);

    auto makeSectionTitle = [&](const QString &title) -> QLabel * {
        auto *lbl = new QLabel(title, page);
        lbl->setStyleSheet(QStringLiteral("font-size:16px; font-weight:700; color:#111827;"));
        return lbl;
    };
    auto makeSectionDesc = [&](const QString &text) -> QLabel * {
        auto *lbl = new QLabel(text, page);
        lbl->setStyleSheet(QStringLiteral("font-size:13px; color:#6B7280;"));
        return lbl;
    };
    auto makeSep = [&]() -> QFrame * {
        auto *sep = new QFrame(page);
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet(QStringLiteral("color:#E5E7EB; margin:20px 0;"));
        return sep;
    };

    // ── Appearance ─────────────────────────────────────────────────────────
    vbox->addWidget(makeSectionTitle(tr("Appearance")));
    vbox->addSpacing(4);
    vbox->addWidget(makeSectionDesc(tr("Choose how OpenPDF Studio is displayed.")));
    vbox->addSpacing(16);

    auto *cardsRow = new QHBoxLayout;
    cardsRow->setSpacing(12);
    cardsRow->setAlignment(Qt::AlignLeft);

    struct ThemeDef { const char *icon; const char *id; const char *title; const char *desc; };
    const ThemeDef themes[] = {
        { "monitor", "system", QT_TR_NOOP("System"),
          QT_TR_NOOP("Follows system settings") },
        { "sun",     "light",  QT_TR_NOOP("Light"),
          QT_TR_NOOP("Light user interface") },
        { "moon",    "dark",   QT_TR_NOOP("Dark"),
          QT_TR_NOOP("Dark user interface") },
    };

    for (const auto &t : themes) {
        QWidget *card = buildThemeCard(
            QLatin1String(t.icon), tr(t.title), tr(t.desc), QLatin1String(t.id));
        cardsRow->addWidget(card);
    }
    cardsRow->addStretch(1);
    vbox->addLayout(cardsRow);

    vbox->addWidget(makeSep());

    // ── Language ───────────────────────────────────────────────────────────
    vbox->addWidget(makeSectionTitle(tr("Language")));
    vbox->addSpacing(4);
    vbox->addWidget(makeSectionDesc(tr("Choose your preferred language.")));
    vbox->addSpacing(14);

    m_langCombo = new QComboBox(page);
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
    vbox->addWidget(m_langCombo);

    vbox->addWidget(makeSep());

    // ── Media Playback (stub) ──────────────────────────────────────────────
    vbox->addWidget(makeSectionTitle(tr("Media Playback")));
    vbox->addSpacing(4);
    vbox->addWidget(makeSectionDesc(tr("Choose how media is played in OpenPDF Studio.")));
    vbox->addSpacing(14);

    auto *comingSoon = new QLabel(tr("Coming soon."), page);
    comingSoon->setStyleSheet(QStringLiteral("color:#9CA3AF; font-size:13px;"));
    vbox->addWidget(comingSoon);

    vbox->addStretch(1);

    scroll->setWidget(page);
    return scroll;
}

QWidget *SettingsPanel::buildThemeCard(const QString &icon, const QString &title,
                                        const QString &desc, const QString &id)
{
    auto *card = new ThemeCard(icon, title, desc, this);
    card->setIconName(icon);
    card->setChecked(id == m_pendingTheme);

    m_themeCards.append(card);
    m_themeIds.append(id);

    connect(card, &ThemeCard::activated, this, [this, id]() {
        m_pendingTheme = id;
        selectThemeCard(id);
        // Live preview
        Q_EMIT themeChangeRequested(id);
    });

    return card;
}

void SettingsPanel::selectThemeCard(const QString &id)
{
    for (int i = 0; i < m_themeCards.size(); ++i)
        qobject_cast<ThemeCard *>(m_themeCards[i])->setChecked(m_themeIds[i] == id);
}

QWidget *SettingsPanel::buildPlaceholderPage(const QString &title)
{
    auto *w = new QWidget(this);
    auto *vbox = new QVBoxLayout(w);
    vbox->setAlignment(Qt::AlignCenter);

    auto *lbl = new QLabel(title, w);
    lbl->setStyleSheet(QStringLiteral("font-size:18px; font-weight:700; color:#9CA3AF;"));
    lbl->setAlignment(Qt::AlignCenter);
    vbox->addWidget(lbl);

    auto *sub = new QLabel(tr("Settings coming soon."), w);
    sub->setStyleSheet(QStringLiteral("font-size:13px; color:#D1D5DB;"));
    sub->setAlignment(Qt::AlignCenter);
    vbox->addWidget(sub);

    return w;
}

QWidget *SettingsPanel::buildAboutPage()
{
    auto *w = new QWidget(this);
    auto *vbox = new QVBoxLayout(w);
    vbox->setContentsMargins(32, 28, 32, 28);
    vbox->setSpacing(10);

    auto *logo = new QLabel(QStringLiteral("O"), w);
    logo->setObjectName(QStringLiteral("AppLogo"));
    logo->setFixedSize(48, 48);
    logo->setAlignment(Qt::AlignCenter);
    vbox->addWidget(logo, 0, Qt::AlignHCenter);

    auto *title = new QLabel(QStringLiteral("OpenPDF Studio"), w);
    title->setStyleSheet(QStringLiteral("font-size:20px; font-weight:700;"));
    title->setAlignment(Qt::AlignCenter);
    vbox->addWidget(title, 0, Qt::AlignHCenter);

    auto *ver = new QLabel(
        QStringLiteral("Version %1").arg(QLatin1String(APP_VERSION)), w);
    ver->setStyleSheet(QStringLiteral("font-size:13px; color:#6B7280;"));
    ver->setAlignment(Qt::AlignCenter);
    vbox->addWidget(ver, 0, Qt::AlignHCenter);

    vbox->addStretch(1);
    return w;
}

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
    QDialog::changeEvent(e);
}
