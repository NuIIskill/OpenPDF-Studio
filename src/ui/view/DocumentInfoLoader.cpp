#include "ui/view/DocumentInfoLoader.hpp"

#include "app/AppSettings.hpp"
#include "app/TrustedSigners.hpp"
#include "engine/sign/SignatureManager.hpp"

#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>

DocumentInfoLoader::DocumentInfoLoader(QObject *parent)
    : QObject(parent)
{
    connect(TrustedSigners::instance(), &TrustedSigners::changed,
            this, &DocumentInfoLoader::reverifySignatures);
}

void DocumentInfoLoader::reverifySignatures()
{
    if (m_loaded && m_info.signedFields > 0) load(m_path, m_password);
}

void DocumentInfoLoader::load(const QString &path, const std::string &password)
{
    m_path     = path;
    m_password = password;
    const int generation = ++m_generation;
    const bool trustOwn = AppSettings().trustOwnCertificates();
    QPointer<DocumentInfoLoader> self(this);
    QThreadPool::globalInstance()->start([self, generation, path, password, trustOwn]() {
        DocumentInfo info = DocumentInfo::read(path, password);
        if (info.signedFields > 0 && SignatureManager::available()) {
            info.signatures        = SignatureManager().verify(path, password, trustOwn);
            info.signaturesChecked = true;
        }
        QMetaObject::invokeMethod(qApp, [self, generation, info]() {
            if (!self || self->m_generation != generation) return;
            self->m_info   = info;
            self->m_loaded = true;
            Q_EMIT self->changed();
        }, Qt::QueuedConnection);
    });
}

void DocumentInfoLoader::clear()
{
    ++m_generation;
    m_info   = {};
    m_loaded = false;
    Q_EMIT changed();
}
