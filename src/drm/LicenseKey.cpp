#include "drm/LicenseKey.hpp"

#include "monocypher/monocypher-ed25519.h"

#include <QByteArray>

namespace {

// The public half of the key that signs license keys.
constexpr License::PublicKey kPublicKey {
    0x56, 0x74, 0xf2, 0xb8, 0xb3, 0x14, 0x6c, 0x87,
    0xb8, 0x93, 0x8f, 0x21, 0x47, 0xec, 0x72, 0xab,
    0x75, 0xed, 0x5e, 0xc9, 0x5e, 0xcd, 0xbf, 0xdc,
    0x67, 0x25, 0xac, 0x0d, 0xcc, 0x7d, 0x22, 0xfe,
};

constexpr auto   kPrefix           = "OPDF1";
constexpr auto   kContext          = "OPDF-LIC-1";
constexpr quint8 kFormatVersion    = 1;
constexpr quint8 kProductRichMedia = 1;
constexpr int    kHeaderSize       = 13;
constexpr int    kSignatureSize    = 64;
const QDate      kEpoch(2025, 1, 1);

QByteArray base32Decode(const QString &text, bool *ok)
{
    static const QByteArray alphabet("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567");
    QByteArray out;
    quint32 buffer = 0;
    int bits = 0;
    for (const QChar c : text) {
        const int v = alphabet.indexOf(c.toLatin1());
        if (v < 0) {
            *ok = false;
            return {};
        }
        buffer = (buffer << 5) | quint32(v);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.append(char((buffer >> bits) & 0xFF));
        }
    }
    *ok = true;
    return out;
}

quint16 readU16(const QByteArray &b, int at)
{
    return quint16(quint8(b[at])) | quint16(quint8(b[at + 1])) << 8;
}

quint32 readU32(const QByteArray &b, int at)
{
    return quint32(readU16(b, at)) | quint32(readU16(b, at + 2)) << 16;
}

}

namespace License {

KeyInfo inspectKey(const QString &key, QDate today)
{
    return inspectKey(key, kPublicKey, today);
}

KeyInfo inspectKey(const QString &key, const PublicKey &publicKey, QDate today)
{
    KeyInfo info;

    QString text;
    for (const QChar c : key) {
        if (!c.isSpace() && c != QLatin1Char('-'))
            text.append(c.toUpper());
    }
    if (!text.startsWith(QLatin1String(kPrefix)))
        return info;

    bool ok = false;
    const QByteArray raw = base32Decode(text.mid(int(qstrlen(kPrefix))), &ok);
    if (!ok || raw.size() < kHeaderSize + kSignatureSize)
        return info;

    const QByteArray payload   = raw.left(raw.size() - kSignatureSize);
    const QByteArray signature = raw.right(kSignatureSize);
    const QByteArray message   = QByteArray(kContext) + payload;
    if (crypto_ed25519_check(reinterpret_cast<const uint8_t *>(signature.constData()),
                             publicKey.data(),
                             reinterpret_cast<const uint8_t *>(message.constData()),
                             size_t(message.size())) != 0) {
        info.status = KeyStatus::BadSignature;
        return info;
    }

    const int nameLength = quint8(payload[12]);
    if (quint8(payload[0]) != kFormatVersion || payload.size() != kHeaderSize + nameLength)
        return info;

    info.serial   = readU32(payload, 2);
    info.seats    = readU16(payload, 6);
    info.issued   = kEpoch.addDays(readU16(payload, 8));
    if (const quint16 expires = readU16(payload, 10))
        info.expires = kEpoch.addDays(expires);
    info.licensee = QString::fromUtf8(payload.mid(kHeaderSize, nameLength));

    if (quint8(payload[1]) != kProductRichMedia)
        info.status = KeyStatus::WrongProduct;
    else if (info.expires.isValid() && today > info.expires)
        info.status = KeyStatus::Expired;
    else
        info.status = KeyStatus::Valid;
    return info;
}

}
