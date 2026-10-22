#include "app/TrustedSigners.hpp"
#include "app/AppConfig.hpp"
#include "app/SafeWrite.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace {

constexpr qsizetype kFingerprintSize = 32;

QString filePath()
{
    return QFileInfo(AppConfig::path()).absoluteDir()
        .filePath(QStringLiteral("trusted-signers.json"));
}

QList<TrustedSigners::Entry> read()
{
    QList<TrustedSigners::Entry> entries;
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) return entries;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        TrustedSigners::Entry e;
        e.fingerprint = QByteArray::fromHex(o.value(QStringLiteral("fingerprint")).toString().toLatin1());
        if (e.fingerprint.size() != kFingerprintSize) continue;
        e.name    = o.value(QStringLiteral("name")).toString();
        e.issuer  = o.value(QStringLiteral("issuer")).toString();
        e.validTo = QDateTime::fromString(o.value(QStringLiteral("validTo")).toString(), Qt::ISODate);
        e.added   = QDateTime::fromString(o.value(QStringLiteral("added")).toString(), Qt::ISODate);
        entries.append(e);
    }
    return entries;
}

bool write(const QList<TrustedSigners::Entry> &entries)
{
    QJsonArray array;
    for (const TrustedSigners::Entry &e : entries) {
        QJsonObject o;
        o.insert(QStringLiteral("fingerprint"), QString::fromLatin1(e.fingerprint.toHex()));
        o.insert(QStringLiteral("name"), e.name);
        o.insert(QStringLiteral("issuer"), e.issuer);
        o.insert(QStringLiteral("validTo"), e.validTo.toString(Qt::ISODate));
        o.insert(QStringLiteral("added"), e.added.toString(Qt::ISODate));
        array.append(o);
    }
    const QByteArray bytes = QJsonDocument(array).toJson();
    const QString target = filePath();
    const QString staging = SafeWrite::stagingPath(target);
    QFile file(staging);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size()) {
        file.close();
        if (SafeWrite::commit(staging, target)) return true;
    }
    file.close();
    SafeWrite::discard(staging);
    return false;
}

}

TrustedSigners *TrustedSigners::instance()
{
    static TrustedSigners *self = new TrustedSigners(QCoreApplication::instance());
    return self;
}

QList<TrustedSigners::Entry> TrustedSigners::list()
{
    return read();
}

bool TrustedSigners::contains(const QByteArray &fingerprint)
{
    if (fingerprint.size() != kFingerprintSize) return false;
    const QList<Entry> entries = read();
    return std::any_of(entries.begin(), entries.end(),
                       [&](const Entry &e) { return e.fingerprint == fingerprint; });
}

bool TrustedSigners::add(Entry entry)
{
    if (entry.fingerprint.size() != kFingerprintSize) return false;
    if (!entry.added.isValid()) entry.added = QDateTime::currentDateTimeUtc();
    QList<Entry> entries = read();
    entries.removeIf([&](const Entry &e) { return e.fingerprint == entry.fingerprint; });
    entries.append(entry);
    if (!write(entries)) return false;
    Q_EMIT instance()->changed();
    return true;
}

bool TrustedSigners::remove(const QByteArray &fingerprint)
{
    QList<Entry> entries = read();
    if (entries.removeIf([&](const Entry &e) { return e.fingerprint == fingerprint; }) == 0)
        return true;
    if (!write(entries)) return false;
    Q_EMIT instance()->changed();
    return true;
}
