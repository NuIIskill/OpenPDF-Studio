#pragma once

#include <QDate>
#include <QString>

#include <array>
#include <cstdint>

namespace License {

enum class KeyStatus { Valid, Malformed, BadSignature, WrongProduct, Expired };

struct KeyInfo
{
    KeyStatus status { KeyStatus::Malformed };
    quint32   serial { 0 };
    QString   licensee;
    int       seats  { 0 };
    QDate     issued;
    QDate     expires;
};

using PublicKey = std::array<std::uint8_t, 32>;

KeyInfo inspectKey(const QString &key, QDate today = QDate::currentDate());
KeyInfo inspectKey(const QString &key, const PublicKey &publicKey, QDate today);

}
