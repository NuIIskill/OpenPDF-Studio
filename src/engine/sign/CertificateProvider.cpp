#include "engine/sign/CertificateProvider.hpp"

#include <QCryptographicHash>

std::unique_ptr<CertificateProvider> createPlatformProvider();

std::unique_ptr<CertificateProvider> CertificateProvider::create()
{
#ifdef HAVE_DIGITAL_SIGNATURE
    return createPlatformProvider();
#else
    return {};
#endif
}

namespace {

QByteArray derItem(char tag, const QByteArray &content)
{
    QByteArray out(1, tag);
    const qsizetype len = content.size();
    if (len < 0x80) {
        out += char(len);
    } else {
        QByteArray digits;
        for (qsizetype v = len; v > 0; v >>= 8)
            digits.prepend(char(v & 0xFF));
        out += char(0x80 | digits.size());
        out += digits;
    }
    return out + content;
}

}

namespace Cms {

QByteArray trimmed(const QByteArray &der)
{
    if (der.size() < 2 || uchar(der[0]) != 0x30) return der;
    const uchar first = uchar(der[1]);
    qsizetype header = 2;
    qsizetype length = first;
    if (first & 0x80) {
        const int count = first & 0x7F;
        if (count == 0 || count > 4 || der.size() < 2 + count) return der;
        length = 0;
        for (int i = 0; i < count; ++i)
            length = (length << 8) | uchar(der[2 + i]);
        header += count;
    }
    return header + length <= der.size() ? der.left(header + length) : der;
}

QByteArray signingCertificateV2(const QByteArray &certificateDer)
{
    const QByteArray hash = QCryptographicHash::hash(certificateDer, QCryptographicHash::Sha256);
    const QByteArray certId = derItem(0x30, derItem(0x04, hash));
    return derItem(0x30, derItem(0x30, certId));
}

}
