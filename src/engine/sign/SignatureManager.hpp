#pragma once

#include "engine/sign/CertificateProvider.hpp"
#include "engine/sign/SignatureVerifier.hpp"

#include <QImage>
#include <QRectF>

struct SignRequest {
    QString certificateId;
    int     page { 0 };
    QRectF  bounds;
    QImage  appearance;
    QString name;
    QString reason;
    QString location;
};

/// Signs PDFs with a certificate from the platform and checks existing signatures.
class SignatureManager
{
public:
    SignatureManager();
    ~SignatureManager();

    static bool available();

    QList<Certificate>     certificates();
    QString                createCertificate(const NewCertificate &request, SignError *error);
    bool                   removeCertificate(const QString &certificateId);
    SignError              sign(const QString &source, const QString &target, const SignRequest &request);
    QList<SignatureStatus> verify(const QString &path);

private:
    std::unique_ptr<CertificateProvider> m_provider;
};
