#include "ui/settings/SettingsPanel.hpp"
#include "app/AppConfig.hpp"
#include "app/LogFile.hpp"

#include <QAbstractButton>
#include <QLabel>
#include <QVBoxLayout>

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

    auto *cfg = new QLabel(AppConfig::isPortable()
        ? tr("Configuration (portable): %1").arg(AppConfig::path())
        : tr("Configuration: %1").arg(AppConfig::path()), page);
    cfg->setObjectName(QStringLiteral("SettingsSectionDesc"));
    cfg->setAlignment(Qt::AlignCenter);
    cfg->setWordWrap(true);
    cfg->setTextInteractionFlags(Qt::TextSelectableByMouse);
    vbox->addWidget(cfg);

    auto *log = new QLabel(tr("Log file: %1").arg(LogFile::path()), page);
    log->setObjectName(QStringLiteral("SettingsSectionDesc"));
    log->setAlignment(Qt::AlignCenter);
    log->setWordWrap(true);
    log->setTextInteractionFlags(Qt::TextSelectableByMouse);
    vbox->addWidget(log);
    if (m_debugLogCheck) {
        log->setVisible(m_debugLogCheck->isChecked());
        connect(m_debugLogCheck, &QAbstractButton::toggled, log, &QWidget::setVisible);
    }
    return page;
}
