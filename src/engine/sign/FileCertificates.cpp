#include "engine/sign/FileCertificates.hpp"
#include "app/AppConfig.hpp"
#include "app/SafeWrite.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace FileCertificates {

namespace {

const QString kPrefix     = QStringLiteral("file:");
const QString kCertSuffix = QStringLiteral(".crt.pem");
const QString kKeySuffix  = QStringLiteral(".key.pem");

QString directory()
{
    const QDir dir(QFileInfo(AppConfig::path()).absoluteDir().filePath(QStringLiteral("certificates")));
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) return {};
    return dir.absolutePath();
}

QString baseName(const QString &id)
{
    const QString name = id.mid(kPrefix.size());
    return name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
            || name.startsWith(QLatin1Char('.'))
        ? QString() : name;
}

QByteArray readFile(const QString &id, const QString &suffix)
{
    const QString dir = directory();
    const QString name = baseName(id);
    if (dir.isEmpty() || name.isEmpty()) return {};
    QFile file(QDir(dir).filePath(name + suffix));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &target, const QByteArray &bytes, bool secret)
{
    const QString staging = SafeWrite::stagingPath(target);
    QFile file(staging);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    if (secret) file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const bool written = file.write(bytes) == bytes.size();
    file.close();
    if (written && SafeWrite::commit(staging, target)) return true;
    SafeWrite::discard(staging);
    return false;
}

}

bool isFileId(const QString &id)
{
    return id.startsWith(kPrefix);
}

QStringList ids()
{
    const QString dir = directory();
    if (dir.isEmpty()) return {};
    QStringList out;
    const QFileInfoList files = QDir(dir).entryInfoList({ QLatin1Char('*') + kCertSuffix },
                                                        QDir::Files, QDir::Time | QDir::Reversed);
    for (const QFileInfo &fi : files) {
        const QString name = fi.fileName().chopped(kCertSuffix.size());
        if (QFileInfo::exists(QDir(dir).filePath(name + kKeySuffix))) out << kPrefix + name;
    }
    return out;
}

QByteArray certificatePem(const QString &id)
{
    return readFile(id, kCertSuffix);
}

QByteArray keyPem(const QString &id)
{
    return readFile(id, kKeySuffix);
}

bool remove(const QString &id)
{
    const QString dir = directory();
    const QString name = baseName(id);
    if (dir.isEmpty() || name.isEmpty()) return false;
    const bool key  = QFile::remove(QDir(dir).filePath(name + kKeySuffix));
    const bool cert = QFile::remove(QDir(dir).filePath(name + kCertSuffix));
    return key && cert;
}

QString store(const QByteArray &certificatePem, const QByteArray &keyPem)
{
    const QString dir = directory();
    if (dir.isEmpty()) return {};
    const QString name = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!writeFile(QDir(dir).filePath(name + kKeySuffix), keyPem, true)) return {};
    if (!writeFile(QDir(dir).filePath(name + kCertSuffix), certificatePem, false)) {
        QFile::remove(QDir(dir).filePath(name + kKeySuffix));
        return {};
    }
    return kPrefix + name;
}

}
