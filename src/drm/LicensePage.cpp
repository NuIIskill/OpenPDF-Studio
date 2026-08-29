#include "drm/LicensePage.hpp"

#include "drm/LicenseStore.hpp"
#include "ui/theme/Theme.hpp"

#include <QAction>
#include <QDesktopServices>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr auto kBuyUrl = "https://openpdf-studio.nullskill.de/license";
}

LicensePage::LicensePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("SettingsScrollContent"));
    buildUi();
    refreshStatus();
    applyTheme();
}

void LicensePage::buildUi()
{
    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(32, 28, 32, 32);
    vbox->setSpacing(0);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("SettingsSectionTitle"));
    vbox->addWidget(m_title);
    vbox->addSpacing(6);

    m_desc = new QLabel(this);
    m_desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    m_desc->setWordWrap(true);
    vbox->addWidget(m_desc);
    vbox->addSpacing(24);

    m_statusGroup = new QLabel(this);
    m_statusGroup->setObjectName(QStringLiteral("SettingsGroupLabel"));
    vbox->addWidget(m_statusGroup);
    vbox->addSpacing(12);

    m_statusCard = new QFrame(this);
    m_statusCard->setObjectName(QStringLiteral("LicenseStatusCard"));
    m_statusCard->setAttribute(Qt::WA_StyledBackground, true);
    m_statusCard->setMinimumHeight(84);
    auto *cardRow = new QHBoxLayout(m_statusCard);
    cardRow->setContentsMargins(20, 16, 20, 16);
    cardRow->setSpacing(14);

    m_statusIcon = new QLabel(m_statusCard);
    m_statusIcon->setFixedSize(28, 28);
    m_statusIcon->setAlignment(Qt::AlignCenter);
    cardRow->addWidget(m_statusIcon, 0, Qt::AlignVCenter);

    auto *cardText = new QVBoxLayout;
    cardText->setContentsMargins(0, 0, 0, 0);
    cardText->setSpacing(3);
    m_statusTitle = new QLabel(m_statusCard);
    m_statusDesc  = new QLabel(m_statusCard);
    m_statusDesc->setWordWrap(true);
    cardText->addWidget(m_statusTitle);
    cardText->addWidget(m_statusDesc);
    cardRow->addLayout(cardText, 1);

    vbox->addWidget(m_statusCard);
    vbox->addSpacing(24);

    m_activateGroup = new QLabel(this);
    m_activateGroup->setObjectName(QStringLiteral("SettingsGroupLabel"));
    vbox->addWidget(m_activateGroup);
    vbox->addSpacing(12);

    m_fieldLabel = new QLabel(this);
    vbox->addWidget(m_fieldLabel);
    vbox->addSpacing(8);

    m_keyInput = new QLineEdit(this);
    m_keyInput->setObjectName(QStringLiteral("LicenseKeyInput"));
    m_keyInput->setFixedHeight(42);
    m_keyIcon = m_keyInput->addAction(QIcon(), QLineEdit::TrailingPosition);
    connect(m_keyInput, &QLineEdit::returnPressed, this, &LicensePage::onActivateClicked);
    connect(m_keyInput, &QLineEdit::textChanged, this, [this](const QString &t) {
        if (!m_hasKey) m_actionBtn->setEnabled(!t.trimmed().isEmpty());
        applyTheme();
    });
    vbox->addWidget(m_keyInput);
    vbox->addSpacing(14);

    m_actionBtn = new QPushButton(this);
    m_actionBtn->setObjectName(QStringLiteral("LicenseActionBtn"));
    m_actionBtn->setFixedHeight(38);
    m_actionBtn->setCursor(Qt::PointingHandCursor);
    connect(m_actionBtn, &QPushButton::clicked, this, &LicensePage::onActivateClicked);
    vbox->addWidget(m_actionBtn, 0, Qt::AlignLeft);
    vbox->addSpacing(8);

    m_feedback = new QLabel(this);
    m_feedback->setWordWrap(true);
    m_feedback->hide();
    vbox->addWidget(m_feedback);
    vbox->addSpacing(20);

    m_needGroup = new QLabel(this);
    m_needGroup->setObjectName(QStringLiteral("SettingsGroupLabel"));
    vbox->addWidget(m_needGroup);
    vbox->addSpacing(10);

    m_needDesc = new QLabel(this);
    m_needDesc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    m_needDesc->setWordWrap(true);
    vbox->addWidget(m_needDesc);
    vbox->addSpacing(12);

    m_getBtn = new QPushButton(this);
    m_getBtn->setObjectName(QStringLiteral("SettingsCancelBtn"));
    m_getBtn->setFixedHeight(36);
    m_getBtn->setCursor(Qt::PointingHandCursor);
    connect(m_getBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(QLatin1String(kBuyUrl)));
    });
    vbox->addWidget(m_getBtn, 0, Qt::AlignLeft);

    vbox->addStretch(1);

    retranslateUi();
}

void LicensePage::refreshStatus()
{
    m_hasKey = License::hasKey();

    if (m_hasKey) {
        m_keyInput->setText(License::key());
        const bool fromSetup = License::keyIsMachineWide();
        m_keyInput->setReadOnly(true);
        m_actionBtn->setVisible(!fromSetup);
        m_actionBtn->setEnabled(true);
        m_actionBtn->setText(tr("Remove License"));
        m_statusTitle->setText(tr("License key on record"));
        m_statusDesc->setText(fromSetup
            ? tr("Set by the installer for every account on this machine.")
            : tr("Entered in this application. Business use is covered."));
    } else {
        m_keyInput->setReadOnly(false);
        m_actionBtn->setVisible(true);
        m_actionBtn->setText(tr("Activate License"));
        m_actionBtn->setEnabled(!m_keyInput->text().trimmed().isEmpty());

        const int left = License::evaluationDaysLeft();
        if (left > 0) {
            m_statusTitle->setText(left == 1
                ? tr("Evaluation — 1 day left")
                : tr("Evaluation — %1 days left").arg(left));
            m_statusDesc->setText(
                tr("Business use is free to evaluate for %1 days. No key needed yet.")
                    .arg(License::kEvaluationDays));
        } else {
            m_statusTitle->setText(tr("Evaluation period ended"));
            m_statusDesc->setText(
                tr("Business use requires a license. Please enter a license key."));
        }
    }

    applyTheme();
}

void LicensePage::onActivateClicked()
{
    if (m_hasKey) {
        License::clearKey();
        m_keyInput->clear();
        m_feedback->setText(tr("License key removed."));
        m_feedback->show();
        refreshStatus();
        return;
    }

    const QString key = m_keyInput->text().trimmed();
    if (key.isEmpty())
        return;

    License::setKey(key);
    m_feedback->setText(tr("License key saved."));
    m_feedback->show();
    refreshStatus();
}

void LicensePage::applyTheme()
{
    const bool dk = Theme::DarkMode;
    const bool warn = !m_hasKey && License::evaluationDaysLeft() == 0;

    const QString cardBg  = warn ? (dk ? "#3A2A12" : "#FFFBEB")
                                 : (dk ? "#2E2E2E" : "#FFFFFF");
    const QString cardBdr = warn ? (dk ? "#78350F" : "#FDE68A")
                                 : (dk ? "#4A4A4A" : "#E5E7EB");
    m_statusCard->setStyleSheet(QStringLiteral(
        "QFrame#LicenseStatusCard { background:%1; border:1px solid %2; border-radius:8px; }")
        .arg(cardBg, cardBdr));

    const QColor iconColor(warn ? (dk ? "#FBBF24" : "#D97706")
                                : (dk ? "#4ADE80" : "#16A34A"));
    const QPixmap px = Theme::renderSvg(
        warn ? QStringLiteral("shield-alert") : QStringLiteral("shield-check"),
        iconColor, 26);
    if (!px.isNull()) m_statusIcon->setPixmap(px);

    m_statusTitle->setStyleSheet(QStringLiteral("font-size:13px; font-weight:600; color:%1;")
        .arg(dk ? "#E8E8E8" : "#111827"));
    m_statusDesc->setStyleSheet(QStringLiteral("font-size:12px; color:%1;")
        .arg(dk ? "#A0A0A0" : "#6B7280"));

    m_fieldLabel->setStyleSheet(QStringLiteral("font-size:13px; font-weight:600; color:%1;")
        .arg(dk ? "#E0E0E0" : "#111827"));

    m_keyInput->setStyleSheet(QStringLiteral(
        "QLineEdit#LicenseKeyInput { background:%1; border:1px solid %2; border-radius:8px;"
        " padding:0 12px; font-size:13px; color:%3; }"
        "QLineEdit#LicenseKeyInput:focus { border:1px solid #3B82F6; }"
        "QLineEdit#LicenseKeyInput:read-only { background:%4; }")
        .arg(dk ? "#2E2E2E" : "#FFFFFF",
             dk ? "#4A4A4A" : "#E5E7EB",
             dk ? "#E8E8E8" : "#111827",
             dk ? "#3A3A3A" : "#F9FAFB"));
    if (m_keyIcon) {
        const QPixmap kpx = Theme::renderSvg(QStringLiteral("key"),
            QColor(dk ? "#9CA3AF" : "#6B7280"), 16);
        if (!kpx.isNull()) m_keyIcon->setIcon(QIcon(kpx));
    }

    if (m_hasKey) {
        m_actionBtn->setStyleSheet(dk
            ? QStringLiteral(
                "QPushButton { background:#3D1A1A; color:#F87171; border:1px solid #7F1D1D;"
                " border-radius:8px; font-size:13px; padding:0 18px; }"
                "QPushButton:hover { background:#4D2020; }")
            : QStringLiteral(
                "QPushButton { background:#FEF2F2; color:#DC2626; border:1px solid #FCA5A5;"
                " border-radius:8px; font-size:13px; padding:0 18px; }"
                "QPushButton:hover { background:#FEE2E2; }"));
    } else {
        m_actionBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background:#2563EB; color:#FFFFFF; border:none; border-radius:8px;"
            " font-size:13px; font-weight:600; padding:0 18px; }"
            "QPushButton:hover { background:#1D4ED8; }"
            "QPushButton:disabled { background:%1; color:%2; }")
            .arg(dk ? "#1E3358" : "#DBEAFE", dk ? "#4B7BC8" : "#93C5FD"));
    }

    m_feedback->setStyleSheet(QStringLiteral("font-size:12px; color:%1;")
        .arg(dk ? "#4ADE80" : "#16A34A"));
}

void LicensePage::retranslateUi()
{
    m_title->setText(tr("License Key"));
    m_desc->setText(tr("This installation is set up for business use. "
                       "Enter your license key here."));
    m_statusGroup->setText(tr("LICENSE STATUS"));
    m_activateGroup->setText(tr("ACTIVATE LICENSE"));
    m_fieldLabel->setText(tr("License Key"));
    m_keyInput->setPlaceholderText(tr("Enter your license key"));
    m_needGroup->setText(tr("NEED A LICENSE?"));
    m_needDesc->setText(tr("A Business License covers business use of OpenPDF Studio "
                           "and supports the development."));
    m_getBtn->setText(tr("Get License"));
    refreshStatus();
}

void LicensePage::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    else if (e->type() == QEvent::StyleChange)
        applyTheme();
    QWidget::changeEvent(e);
}
