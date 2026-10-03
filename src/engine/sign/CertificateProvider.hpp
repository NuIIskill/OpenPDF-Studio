#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QDateTime>
#include <QList>
#include <QString>

#include <memory>

struct Certificate {
    QString   id;
    QString   subject;
    QString   distinguishedName;
    QString   issuer;
    QDateTime validTo;
    bool      removable { false };
};

enum class SignError {
    None,
    NotAvailable,
    CertificateNotFound,
    PinRequired,
    WrongPin,
    PinLocked,
    SigningFailed,
    Encrypted,
    ReadFailed,
    WriteFailed,
};

struct NewCertificate {
    QString name;
    QString organization;
    QString email;
    int     years { 3 };
};

struct CmsCheck {
    bool    intact  { false };
    bool    trusted { false };
    QString signer;
};

/// The platform's certificates and crypto: PKCS#11 through p11-kit on Linux, the certificate store through CNG on Windows.
class CertificateProvider
{
public:
    virtual ~CertificateProvider() = default;

    static std::unique_ptr<CertificateProvider> create();

    virtual QList<Certificate> certificates() = 0;
    virtual QByteArray sign(const QString &certificateId, const QList<QByteArrayView> &data,
                            SignError *error) = 0;
    virtual CmsCheck check(const QByteArray &cms, const QList<QByteArrayView> &data) = 0;
    virtual QString create(const NewCertificate &request, SignError *error) = 0;
    virtual bool remove(const QString &certificateId) = 0;
};

namespace Cms {

QByteArray trimmed(const QByteArray &der);
QByteArray signingCertificateV2(const QByteArray &certificateDer);

}
