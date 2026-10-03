#include "ui/sign/DigitalSignPage.hpp"
#include "ui/sign/CertificateCombo.hpp"
#include "ui/sign/CreateCertificateDialog.hpp"
#include "ui/sign/ProfileCombo.hpp"
#include "ui/sign/SignOption.hpp"
#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QLabel *fieldLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("SField"));
    return label;
}

QLabel *mutedLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("SMuted"));
    label->setWordWrap(true);
    return label;
}

QFrame *divider()
{
    auto *line = new QFrame;
    line->setObjectName(QStringLiteral("SDivider"));
    line->setFixedHeight(1);
    return line;
}

SignatureAppearance toAppearance(const SignatureProfileStore::Profile &p)
{
    SignatureAppearance a;
    a.name = p.signer;
    a.reason = p.reason;
    a.location = p.location;
    a.logo = p.logo;
    a.logoName = p.logoName;
    a.showName = p.showSigner;
    a.showDate = p.showDate;
    a.showReason = p.showReason;
    a.showLocation = p.showLocation;
    a.showLogo = p.showLogo;
    return a;
}

void applyAppearance(SignatureProfileStore::Profile &p, const SignatureAppearance &a)
{
    p.signer = a.name;
    p.reason = a.reason;
    p.location = a.location;
    p.logo = a.logo;
    p.logoName = a.logoName;
    p.showSigner = a.showName;
    p.showDate = a.showDate;
    p.showReason = a.showReason;
    p.showLocation = a.showLocation;
    p.showLogo = a.showLogo;
}

QLabel *iconLabel(const QString &icon, int size)
{
    auto *label = new QLabel;
    label->setPixmap(Theme::renderSvg(icon, Theme::IconNormal, size));
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    return label;
}

}

DigitalSignPage::DigitalSignPage(QWidget *parent)
    : QWidget(parent)
{
    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(24, 14, 24, 14);
    vl->setSpacing(8);

    vl->addWidget(fieldLabel(tr("Select certificate")));
    vl->addWidget(buildCertificateRow());
    vl->addSpacing(2);
    vl->addWidget(divider());
    vl->addWidget(buildProfileSection());
    vl->addSpacing(2);
    vl->addWidget(divider());
    vl->addWidget(buildAdvancedHeader());
    m_advancedCard = buildAdvancedCard();
    vl->addWidget(m_advancedCard);
    vl->addStretch(1);
}

QWidget *DigitalSignPage::buildCertificateRow()
{
    auto *row = new QWidget;
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(12);

    m_certCombo = new CertificateCombo;
    m_certCombo->setPlaceholderText(tr("Please select a certificate..."));
    connect(m_certCombo, &QComboBox::currentIndexChanged, this, &DigitalSignPage::changed);
    connect(m_certCombo, &QComboBox::activated, this, &DigitalSignPage::onCertificateActivated);
    connect(m_certCombo, &CertificateCombo::menuRequested, this, &DigitalSignPage::showCertificateMenu);

    m_deleteBtn = new QPushButton;
    m_deleteBtn->setObjectName(QStringLiteral("SIconBtn"));
    m_deleteBtn->setFixedSize(48, 48);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setIcon(Theme::makeIcon(QStringLiteral("trash-2"), Theme::IconNormal,
                                         Theme::IconNormal, Theme::IconDisabled, 18));
    m_deleteBtn->setIconSize(QSize(18, 18));
    m_deleteBtn->setEnabled(false);
    connect(m_deleteBtn, &QPushButton::clicked, this, [this]() {
        deleteCertificate(m_certCombo->currentIndex());
    });
    connect(m_certCombo, &QComboBox::currentIndexChanged, this, &DigitalSignPage::updateDeleteButton);

    hl->addWidget(m_certCombo, 1);
    hl->addWidget(m_deleteBtn);
    return row;
}

QWidget *DigitalSignPage::buildAdvancedHeader()
{
    auto *header = new QPushButton;
    header->setObjectName(QStringLiteral("SAdvHeader"));
    header->setCursor(Qt::PointingHandCursor);
    header->setFixedHeight(50);

    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(0, 2, 0, 2);
    hl->setSpacing(14);
    hl->addWidget(iconLabel(QStringLiteral("settings"), 22), 0, Qt::AlignTop);

    auto *texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto *title = new QLabel(tr("Advanced"));
    title->setObjectName(QStringLiteral("SAdvTitle"));
    auto *subtitle = mutedLabel(tr("Additional signing options."));
    for (QLabel *label : { title, subtitle })
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    texts->addWidget(title);
    texts->addWidget(subtitle);
    hl->addLayout(texts, 1);

    m_chevron = iconLabel(QStringLiteral("chevron-up"), 18);
    hl->addWidget(m_chevron, 0, Qt::AlignTop);

    connect(header, &QPushButton::clicked, this, [this]() {
        const bool open = !m_advancedCard->isVisible();
        m_advancedCard->setVisible(open);
        m_chevron->setPixmap(Theme::renderSvg(open ? QStringLiteral("chevron-up")
                                                   : QStringLiteral("chevron-down"),
                                              Theme::IconNormal, 18));
    });
    return header;
}

QWidget *DigitalSignPage::buildAdvancedCard()
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("SAdvCard"));
    auto *vl = new QVBoxLayout(card);
    vl->setContentsMargins(20, 12, 20, 12);
    vl->setSpacing(6);

    const QString notYet = tr("Not available yet");

    auto *timestamp = new SignOption(tr("Add timestamp"),
                                     tr("Include a trusted timestamp in the signature."));
    auto *lock = new SignOption(tr("Lock document after signing"),
                                tr("Prevent further changes to the PDF."));
    for (SignOption *option : { timestamp, lock }) {
        option->setEnabled(false);
        option->setToolTip(notYet);
    }
    m_invisible = new SignOption(tr("Invisible signature"),
                                 tr("Sign the document without a visible signature."));
    connect(m_invisible, &SignOption::toggled, this, [this](bool invisible) {
        m_profileCombo->setEnabled(!invisible);
        Q_EMIT changed();
    });

    auto *options = new QHBoxLayout;
    options->setSpacing(24);
    for (SignOption *option : { timestamp, lock, m_invisible })
        options->addWidget(option, 1);
    vl->addLayout(options);
    return card;
}

QWidget *DigitalSignPage::buildProfileSection()
{
    auto *section = new QWidget;
    auto *vl = new QVBoxLayout(section);
    vl->setContentsMargins(0, 4, 0, 4);
    vl->setSpacing(8);

    auto *title = new QLabel(tr("Profile"));
    title->setObjectName(QStringLiteral("SAdvTitle"));
    vl->addWidget(title);

    m_profileCombo = new ProfileCombo;
    connect(m_profileCombo, &ProfileCombo::profileChosen, this, &DigitalSignPage::selectProfile);
    connect(m_profileCombo, &ProfileCombo::commandChosen, this, &DigitalSignPage::onProfileCommand);
    vl->addWidget(m_profileCombo);

    loadProfiles(SignatureProfileStore::selected());
    return section;
}

QString DigitalSignPage::profileName(const SignatureProfileStore::Profile &profile) const
{
    if (!profile.name.isEmpty()) return profile.name;
    return profile.id == SignatureProfileStore::kStandardId ? tr("Default") : tr("Unnamed profile");
}

void DigitalSignPage::loadProfiles(const QString &selectId)
{
    m_profiles = SignatureProfileStore::list();
    QList<ProfileCombo::Entry> entries;
    QString current = SignatureProfileStore::kStandardId;
    for (const SignatureProfileStore::Profile &p : std::as_const(m_profiles)) {
        entries.append({ p.id, profileName(p), p.showLogo ? p.logo : QImage() });
        if (p.id == selectId) current = p.id;
    }
    m_profileCombo->setProfiles(entries, current, current != SignatureProfileStore::kStandardId);
}

SignatureProfileStore::Profile DigitalSignPage::currentProfile() const
{
    const QString id = m_profileCombo->currentId();
    for (const SignatureProfileStore::Profile &p : m_profiles)
        if (p.id == id) return p;
    return m_profiles.value(0);
}

void DigitalSignPage::selectProfile(const QString &id)
{
    SignatureProfileStore::setSelected(id);
    loadProfiles(id);
}

void DigitalSignPage::onProfileCommand(ProfileCombo::Command command)
{
    SignatureProfileStore::Profile profile = currentProfile();
    switch (command) {
    case ProfileCombo::Command::New:
    case ProfileCombo::Command::Edit: {
        const bool isNew = command == ProfileCombo::Command::New;
        if (isNew) profile = {};
        SignatureAppearanceDialog dlg(toAppearance(profile), isNew ? QString() : profileName(profile),
                                      certificateName(), isNew, this);
        if (dlg.exec() != QDialog::Accepted) return;
        applyAppearance(profile, dlg.appearance());
        profile.name = dlg.profileName();
        if (profile.name.isEmpty() && isNew) profile.name = tr("Profile %1").arg(m_profiles.size() + 1);
        const QString id = SignatureProfileStore::save(profile);
        if (!id.isEmpty()) selectProfile(id);
        break;
    }
    case ProfileCombo::Command::Duplicate: {
        const QString name = profileName(profile);
        profile.id.clear();
        profile.created = {};
        profile.name = tr("%1 (copy)").arg(name);
        const QString id = SignatureProfileStore::save(profile);
        if (!id.isEmpty()) selectProfile(id);
        break;
    }
    case ProfileCombo::Command::Delete:
        if (profile.id == SignatureProfileStore::kStandardId) return;
        if (QMessageBox::question(this, tr("Delete profile"),
                                  tr("Delete the profile \"%1\"?").arg(profileName(profile)))
                != QMessageBox::Yes)
            return;
        SignatureProfileStore::remove(profile.id);
        selectProfile(SignatureProfileStore::kStandardId);
        break;
    }
}

void DigitalSignPage::loadCertificatesOnce()
{
    if (!m_loaded) loadCertificates();
}

void DigitalSignPage::loadCertificates()
{
    loadCertificates(m_certCombo->currentData().toString());
}

void DigitalSignPage::loadCertificates(const QString &selectId)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_certs = SignatureManager().certificates();
    QApplication::restoreOverrideCursor();
    m_loaded = true;

    const QSignalBlocker block(m_certCombo);
    m_certCombo->clear();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const Certificate &cert : std::as_const(m_certs)) {
        QString label = cert.subject.isEmpty() ? tr("Unnamed certificate") : cert.subject;
        if (cert.validTo.isValid() && cert.validTo < now)
            label = tr("%1, expired").arg(label);
        m_certCombo->addItem(label, cert.id);
        const int index = m_certCombo->count() - 1;
        m_certCombo->setItemData(index, cert.distinguishedName, CertificateCombo::DetailRole);
        m_certCombo->setItemData(index,
                                 tr("Valid until %1").arg(QLocale().toString(
                                     cert.validTo.toLocalTime().date(), QLocale::ShortFormat)),
                                 Qt::ToolTipRole);
    }
    m_certCombo->addNewEntry(tr("New certificate..."));
    m_certCombo->setPlaceholderText(m_certs.isEmpty() ? tr("No certificates found")
                                                      : tr("Please select a certificate..."));
    const int wanted = m_certCombo->findData(selectId);
    m_certCombo->setCurrentIndex(wanted >= 0 ? wanted : m_certs.size() == 1 ? 0 : -1);
    m_currentCert = m_certCombo->currentIndex();
    updateDeleteButton();
    m_certCombo->update();
    Q_EMIT changed();
}

QString DigitalSignPage::certificateName() const
{
    const int index = m_certCombo->currentIndex();
    return index >= 0 && index < m_certs.size() ? m_certs[index].subject : QString();
}

bool DigitalSignPage::isReady() const
{
    const int index = m_certCombo->currentIndex();
    return index >= 0 && !m_certCombo->isNewEntry(index);
}

void DigitalSignPage::showCertificateMenu(int index, const QPoint &globalPos)
{
    if (index < 0 || index >= m_certs.size()) return;
    const Certificate cert = m_certs[index];

    QMenu menu(this);
    menu.setToolTipsVisible(true);
    QAction *remove = menu.addAction(tr("Delete certificate..."));
    remove->setEnabled(cert.removable);
    if (!cert.removable)
        remove->setToolTip(tr("Only certificates created in OpenPDF Studio can be deleted."));
    if (menu.exec(globalPos) != remove) return;

    m_certCombo->hidePopup();
    deleteCertificate(index);
}

void DigitalSignPage::updateDeleteButton()
{
    const int index = m_certCombo->currentIndex();
    const bool removable = index >= 0 && index < m_certs.size() && m_certs[index].removable;
    m_deleteBtn->setEnabled(removable);
    m_deleteBtn->setToolTip(removable || index < 0 || index >= m_certs.size()
                                ? tr("Delete certificate")
                                : tr("Only certificates created in OpenPDF Studio can be deleted."));
}

void DigitalSignPage::deleteCertificate(int index)
{
    if (index < 0 || index >= m_certs.size() || !m_certs[index].removable) return;
    const Certificate cert = m_certs[index];
    const QString name = cert.subject.isEmpty() ? tr("Unnamed certificate") : cert.subject;
    if (QMessageBox::question(this, tr("Delete certificate"),
                              tr("Delete the certificate \"%1\"? Its private key is deleted as "
                                 "well, so it can no longer sign. Signatures already made with "
                                 "it stay valid.").arg(name))
            != QMessageBox::Yes)
        return;
    if (!SignatureManager().removeCertificate(cert.id)) {
        QMessageBox::warning(this, tr("Delete certificate"),
                             tr("The certificate could not be deleted."));
        return;
    }
    const QString keep = m_certCombo->currentData().toString();
    loadCertificates(keep == cert.id ? QString() : keep);
    Q_EMIT changed();
}

void DigitalSignPage::onCertificateActivated(int index)
{
    if (!m_certCombo->isNewEntry(index)) {
        m_currentCert = index;
        return;
    }
    {
        const QSignalBlocker block(m_certCombo);
        m_certCombo->setCurrentIndex(m_currentCert);
    }
    CreateCertificateDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) loadCertificates(dlg.certificateId());
    Q_EMIT changed();
}

bool DigitalSignPage::isInvisible() const
{
    return m_invisible->isChecked();
}

SignRequest DigitalSignPage::signRequest() const
{
    SignRequest request;
    request.certificateId = m_certCombo->currentData().toString();
    if (isInvisible()) return request;

    const SignatureAppearance look = appearance();
    request.name       = look.name;
    request.reason     = look.reason;
    request.location   = look.location;
    request.appearance = look.render(QDateTime::currentDateTime());
    return request;
}

SignatureAppearance DigitalSignPage::appearance() const
{
    SignatureAppearance look = toAppearance(currentProfile());
    if (look.name.isEmpty())   look.name = certificateName();
    if (look.reason.isEmpty()) look.reason = SignatureAppearanceDialog::reasons().first();
    return look;
}
