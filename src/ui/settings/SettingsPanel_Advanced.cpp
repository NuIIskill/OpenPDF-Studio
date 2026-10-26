#include "ui/settings/SettingsPanel.hpp"
#include "app/AppSettings.hpp"
#include "app/UpdateChecker.hpp"
#include "ui/theme/Theme.hpp"
#include "ui/settings/SettingCheckBox.hpp"

#include <QAbstractButton>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

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

    m_updateIntervalCombo->setEnabled(m_autoUpdateCheck->isChecked());
    connect(m_autoUpdateCheck, &QAbstractButton::toggled,
            m_updateIntervalCombo, &QWidget::setEnabled);

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

    connect(resetAllBtn, &QPushButton::clicked, this, [this]() {
        m_pendingTheme = QStringLiteral("system");
        selectCardGroup(m_pendingTheme, m_themeCards, m_themeIds);
        Q_EMIT themeChangeRequested(m_pendingTheme);
        m_pendingLang = AppSettings::systemDefaultLanguage();
        selectLangCode(m_pendingLang);
        if (m_autoUpdateCheck)     m_autoUpdateCheck->setChecked(true);
        if (m_updateIntervalCombo) selectComboData(m_updateIntervalCombo,
                                              QStringLiteral("daily"));
        if (m_preserveLayoutCheck) m_preserveLayoutCheck->setChecked(true);
        if (m_debugLogCheck)       m_debugLogCheck->setChecked(false);
        if (m_logLevelCombo)       selectComboData(m_logLevelCombo,
                                              QStringLiteral("info"));
        if (m_readModeCheck)       m_readModeCheck->setChecked(true);
        if (m_externalLinksCombo)  selectComboData(m_externalLinksCombo,
                                              QStringLiteral("ask"));
        if (m_permissionsCheck)    m_permissionsCheck->setChecked(true);
        if (m_trustOwnCheck)       m_trustOwnCheck->setChecked(true);
    });
    cl->addWidget(resetAllBtn, 0, Qt::AlignLeft);
}

QWidget *SettingsPanel::buildAdvancedPage()
{
    QWidget *page = nullptr;
    QVBoxLayout *vl = buildScrollPage(page, tr("Advanced"), tr("Advanced settings for OpenPDF Studio."));

    buildAdvancedUpdates(vl);
    buildAdvancedInterface(vl);
    buildAdvancedDiagnostics(vl);
    buildAdvancedReset(vl);

    finishScrollPage(vl);
    return page;
}
