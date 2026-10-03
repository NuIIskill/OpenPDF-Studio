#pragma once

#include "engine/sign/SignatureManager.hpp"
#include "app/SignatureProfileStore.hpp"
#include "ui/sign/ProfileCombo.hpp"
#include "ui/sign/SignatureAppearanceDialog.hpp"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
QT_END_NAMESPACE

class CertificateCombo;
class SignOption;

/// The "Sign digitally" page: the certificate, the signing options and how the signature looks.
class DigitalSignPage : public QWidget
{
    Q_OBJECT

public:
    explicit DigitalSignPage(QWidget *parent = nullptr);

    void        loadCertificatesOnce();
    bool        isReady() const;
    bool        isInvisible() const;
    SignRequest signRequest() const;
    SignatureAppearance appearance() const;

Q_SIGNALS:
    void changed();

private:
    QWidget *buildCertificateRow();
    QWidget *buildAdvancedHeader();
    QWidget *buildAdvancedCard();
    QWidget *buildProfileSection();

    void    loadProfiles(const QString &selectId);
    void    selectProfile(const QString &id);
    void    onProfileCommand(ProfileCombo::Command command);
    QString profileName(const SignatureProfileStore::Profile &profile) const;
    SignatureProfileStore::Profile currentProfile() const;

    void    loadCertificates();
    void    loadCertificates(const QString &selectId);
    void    onCertificateActivated(int index);
    void    showCertificateMenu(int index, const QPoint &globalPos);
    void    deleteCertificate(int index);
    void    updateDeleteButton();
    QString certificateName() const;

    QList<Certificate> m_certs;
    bool               m_loaded { false };
    QList<SignatureProfileStore::Profile> m_profiles;
    int                m_currentCert { -1 };

    CertificateCombo *m_certCombo     { nullptr };
    ProfileCombo     *m_profileCombo  { nullptr };
    QPushButton      *m_deleteBtn     { nullptr };
    QWidget          *m_advancedCard  { nullptr };
    QLabel           *m_chevron       { nullptr };
    SignOption       *m_invisible     { nullptr };
};
