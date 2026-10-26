#include "ui/view/ProtectionLayer.hpp"

#include <QDataStream>
#include <QIODevice>
#include <QUuid>

namespace {

constexpr quint32 kStateVersion = 1;

}

ProtectionLayer::ProtectionLayer(QObject *parent)
    : QObject(parent)
{
}

void ProtectionLayer::set(const PdfProtection &protection)
{
    m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_secrets.insert(m_token, { protection.userPassword, protection.ownerPassword });
    m_pending = protection;
    reportChange(tr("Protection changed"), -1);
}

void ProtectionLayer::setDocument(const QString &path)
{
    m_pending.reset();
    m_token.clear();
    if (path.isEmpty()) m_ownerPassword.clear();
}

QString ProtectionLayer::stateKey() const
{
    return QStringLiteral("protection");
}

QByteArray ProtectionLayer::state() const
{
    if (!m_pending) return {};
    const PdfProtection &p = *m_pending;
    const DocumentInfo::Permissions &a = p.permissions;
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << kStateVersion << m_token << p.encrypt << qint32(p.cipher) << p.encryptMetadata
        << !p.userPassword.isEmpty() << !p.ownerPassword.isEmpty()
        << a.print << a.printHighRes << a.modify << a.copy << a.annotate << a.fillForms
        << a.assemble << a.accessibility;
    return bytes;
}

void ProtectionLayer::restoreState(const QByteArray &state)
{
    if (state == this->state()) return;
    m_pending.reset();
    m_token.clear();
    if (state.isEmpty()) return;

    QDataStream in(state);
    quint32 version = 0;
    QString token;
    PdfProtection p;
    qint32 cipher = 0;
    bool hasUser = false, hasOwner = false;
    DocumentInfo::Permissions &a = p.permissions;
    in >> version >> token >> p.encrypt >> cipher >> p.encryptMetadata >> hasUser >> hasOwner
       >> a.print >> a.printHighRes >> a.modify >> a.copy >> a.annotate >> a.fillForms
       >> a.assemble >> a.accessibility;
    if (version != kStateVersion || in.status() != QDataStream::Ok) return;
    p.cipher = PdfProtection::Cipher(cipher);

    // After a crash the passwords are gone, so the document is saved unprotected.
    const auto secret = m_secrets.constFind(token);
    if ((hasUser || hasOwner) && secret == m_secrets.constEnd()) {
        p = PdfProtection{};
    } else if (secret != m_secrets.constEnd()) {
        p.userPassword  = secret->user;
        p.ownerPassword = secret->owner;
    }
    m_token   = token;
    m_pending = p;
}
