#pragma once

#include "engine/sign/CertificateProvider.hpp"

#include <string>

struct SignatureStatus {
    enum class State { Valid, Untrusted, Invalid };

    QString    field;
    QString    signer;
    QString    reason;
    QString    location;
    QDateTime  time;
    QString    issuer;
    QDateTime  validTo;
    QByteArray fingerprint;
    State      state { State::Invalid };
    bool       coversWholeFile { false };
};

/// Finds the signatures in a PDF and checks that each still matches the bytes it signed.
namespace SignatureVerifier {

QList<SignatureStatus> verify(const QString &path, CertificateProvider &provider,
                              const std::string &password = {}, bool trustOwn = true);

}
