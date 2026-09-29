#include "engine/sign/SignatureManager.hpp"
#include "app/SafeWrite.hpp"
#include "engine/sign/PdfSignatureWriter.hpp"

#include <QFile>

namespace {

constexpr int kContentsBytes = 16384;

}

SignatureManager::SignatureManager()
    : m_provider(CertificateProvider::create())
{
}

SignatureManager::~SignatureManager() = default;

bool SignatureManager::available()
{
#if defined(HAVE_DIGITAL_SIGNATURE) && defined(HAVE_QPDF)
    return true;
#else
    return false;
#endif
}

QList<Certificate> SignatureManager::certificates()
{
    return m_provider ? m_provider->certificates() : QList<Certificate>();
}

QString SignatureManager::createCertificate(const NewCertificate &request, SignError *error)
{
    if (!m_provider || request.name.trimmed().isEmpty()) {
        if (error) *error = m_provider ? SignError::SigningFailed : SignError::NotAvailable;
        return {};
    }
    return m_provider->create(request, error);
}

bool SignatureManager::removeCertificate(const QString &certificateId)
{
    return m_provider && m_provider->remove(certificateId);
}

SignError SignatureManager::sign(const QString &source, const QString &target,
                                 const SignRequest &request)
{
    if (!available() || !m_provider) return SignError::NotAvailable;

    PdfSignatureWriter::Field field;
    field.page       = request.page;
    field.bounds     = request.bounds;
    field.appearance = request.appearance;
    field.name       = request.name;
    field.reason     = request.reason;
    field.location   = request.location;
    field.time       = QDateTime::currentDateTime();

    PdfSignatureWriter writer;
    int capacity = kContentsBytes;
    for (int attempt = 0;; ++attempt) {
        const SignError prepared = writer.prepare(source, field, capacity);
        if (prepared != SignError::None) return prepared;

        SignError error = SignError::SigningFailed;
        const QByteArray cms = m_provider->sign(request.certificateId, writer.signedRanges(),
                                                &error);
        if (cms.isEmpty()) return error == SignError::None ? SignError::SigningFailed : error;
        if (writer.embed(cms)) break;
        if (attempt > 0) return SignError::SigningFailed;
        capacity = int(cms.size()) + 2048;
    }

    const QString staging = SafeWrite::stagingPath(target);
    if (staging.isEmpty()) return SignError::WriteFailed;
    QFile out(staging);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || out.write(writer.data()) != writer.data().size()) {
        out.close();
        SafeWrite::discard(staging);
        return SignError::WriteFailed;
    }
    out.close();

    const QList<SignatureStatus> signatures = verify(staging);
    if (signatures.isEmpty() || signatures.last().state == SignatureStatus::State::Invalid
            || !signatures.last().coversWholeFile) {
        SafeWrite::discard(staging);
        return SignError::SigningFailed;
    }
    return SafeWrite::commit(staging, target) ? SignError::None : SignError::WriteFailed;
}

QList<SignatureStatus> SignatureManager::verify(const QString &path)
{
    return m_provider ? SignatureVerifier::verify(path, *m_provider) : QList<SignatureStatus>();
}
