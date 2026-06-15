#include "SettingsPanel.hpp"
#include "app/AppSettings.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QFrame>
#include <QEvent>

SettingsPanel::SettingsPanel(AppSettings *settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setObjectName(QStringLiteral("SettingsPanel"));
    setWindowTitle(tr("Settings"));
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(true);
    buildUi();
}

void SettingsPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    // ── Theme ─────────────────────────────────────────────────────────────
    auto *themeTitle = new QLabel(tr("Appearance"), this);
    themeTitle->setObjectName(QStringLiteral("SettingsSectionTitle"));
    root->addWidget(themeTitle);

    struct ThemeOpt { const char *id; const char *label; };
    const ThemeOpt themes[] = {
        { "system", QT_TR_NOOP("System (auto)") },
        { "light",  QT_TR_NOOP("Light")         },
        { "dark",   QT_TR_NOOP("Dark")          },
    };

    const QString curTheme = m_settings->theme();
    auto *themeGroup = new QButtonGroup(this);
    for (const auto &opt : themes) {
        auto *btn = new QPushButton(tr(opt.label), this);
        btn->setObjectName(QStringLiteral("ThemeOptionBtn"));
        btn->setCheckable(true);
        btn->setChecked(QLatin1String(opt.id) == curTheme);
        btn->setFlat(true);
        themeGroup->addButton(btn);
        const QString id = QLatin1String(opt.id);
        connect(btn, &QPushButton::clicked, this, [this, id]() {
            m_settings->setTheme(id);
            m_settings->sync();
            Q_EMIT themeChangeRequested(id);
        });
        root->addWidget(btn);
    }

    // ── Separator ──────────────────────────────────────────────────────────
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    root->addWidget(sep);

    // ── Language ───────────────────────────────────────────────────────────
    auto *langTitle = new QLabel(tr("Language"), this);
    langTitle->setObjectName(QStringLiteral("SettingsSectionTitle"));
    root->addWidget(langTitle);

    struct LangOpt { const char *id; const char *label; };
    const LangOpt langs[] = {
        { "en", QT_TR_NOOP("English") },
        { "de", QT_TR_NOOP("German")  },
    };

    const QString curLang = m_settings->language();
    auto *langGroup = new QButtonGroup(this);
    for (const auto &opt : langs) {
        auto *btn = new QPushButton(tr(opt.label), this);
        btn->setObjectName(QStringLiteral("LangOptionBtn"));
        btn->setCheckable(true);
        btn->setChecked(QLatin1String(opt.id) == curLang);
        btn->setFlat(true);
        langGroup->addButton(btn);
        const QString id = QLatin1String(opt.id);
        connect(btn, &QPushButton::clicked, this, [this, id]() {
            m_settings->setLanguage(id);
            m_settings->sync();
            Q_EMIT languageChangeRequested(id);
        });
        root->addWidget(btn);
    }

    // ── Close ──────────────────────────────────────────────────────────────
    root->addSpacing(8);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    root->addWidget(closeBtn);

    setFixedWidth(260);
    adjustSize();
}

void SettingsPanel::retranslateUi()
{
    setWindowTitle(tr("Settings"));
}

void SettingsPanel::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(e);
}
