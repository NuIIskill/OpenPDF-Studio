#include "app/SignatureProfileStore.hpp"
#include "app/AppConfig.hpp"
#include "app/SafeWrite.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <algorithm>

namespace SignatureProfileStore {

namespace {

const QString kSelectedFile = QStringLiteral("selected");

QString directory()
{
    const QDir dir(QFileInfo(AppConfig::path()).absoluteDir()
                       .filePath(QStringLiteral("signature-profiles")));
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) return {};
    return dir.absolutePath();
}

bool validId(const QString &id)
{
    return !id.isEmpty() && !id.contains(QLatin1Char('/')) && !id.contains(QLatin1Char('\\'))
        && !id.startsWith(QLatin1Char('.'));
}

bool writeFile(const QString &target, const QByteArray &bytes)
{
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

Profile read(const QString &dir, const QString &id)
{
    Profile p;
    p.id = id;
    QFile file(QDir(dir).filePath(id + QStringLiteral(".json")));
    if (!file.open(QIODevice::ReadOnly)) return p;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    p.name         = o.value(QStringLiteral("name")).toString();
    p.signer       = o.value(QStringLiteral("signer")).toString();
    p.reason       = o.value(QStringLiteral("reason")).toString();
    p.location     = o.value(QStringLiteral("location")).toString();
    p.logoName     = o.value(QStringLiteral("logoName")).toString();
    p.showSigner   = o.value(QStringLiteral("showSigner")).toBool(true);
    p.showDate     = o.value(QStringLiteral("showDate")).toBool(true);
    p.showReason   = o.value(QStringLiteral("showReason")).toBool(true);
    p.showLocation = o.value(QStringLiteral("showLocation")).toBool(true);
    p.showLogo     = o.value(QStringLiteral("showLogo")).toBool(false);
    p.created      = QDateTime::fromString(o.value(QStringLiteral("created")).toString(),
                                           Qt::ISODateWithMs);
    const QString logo = QDir(dir).filePath(id + QStringLiteral(".png"));
    if (QFileInfo::exists(logo)) p.logo = QImage(logo);
    return p;
}

}

QList<Profile> list()
{
    const QString dir = directory();
    QList<Profile> profiles;
    bool haveStandard = false;
    if (!dir.isEmpty()) {
        const QFileInfoList files = QDir(dir).entryInfoList({ QStringLiteral("*.json") }, QDir::Files);
        for (const QFileInfo &fi : files) {
            profiles.append(read(dir, fi.completeBaseName()));
            haveStandard = haveStandard || fi.completeBaseName() == kStandardId;
        }
    }
    if (!haveStandard) {
        Profile standard;
        standard.id = kStandardId;
        profiles.append(standard);
    }
    std::sort(profiles.begin(), profiles.end(), [](const Profile &a, const Profile &b) {
        if ((a.id == kStandardId) != (b.id == kStandardId)) return a.id == kStandardId;
        return a.created != b.created ? a.created < b.created : a.id < b.id;
    });
    return profiles;
}

QString save(Profile profile)
{
    const QString dir = directory();
    if (dir.isEmpty()) return {};
    if (profile.id.isEmpty()) profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!validId(profile.id)) return {};
    if (!profile.created.isValid()) profile.created = QDateTime::currentDateTime();

    QJsonObject o;
    o.insert(QStringLiteral("name"), profile.name);
    o.insert(QStringLiteral("signer"), profile.signer);
    o.insert(QStringLiteral("reason"), profile.reason);
    o.insert(QStringLiteral("location"), profile.location);
    o.insert(QStringLiteral("logoName"), profile.logoName);
    o.insert(QStringLiteral("showSigner"), profile.showSigner);
    o.insert(QStringLiteral("showDate"), profile.showDate);
    o.insert(QStringLiteral("showReason"), profile.showReason);
    o.insert(QStringLiteral("showLocation"), profile.showLocation);
    o.insert(QStringLiteral("showLogo"), profile.showLogo);
    o.insert(QStringLiteral("created"), profile.created.toString(Qt::ISODateWithMs));

    const QString logoPath = QDir(dir).filePath(profile.id + QStringLiteral(".png"));
    if (profile.logo.isNull()) {
        QFile::remove(logoPath);
    } else {
        const QString staging = SafeWrite::stagingPath(logoPath);
        if (!profile.logo.save(staging, "PNG") || !SafeWrite::commit(staging, logoPath)) {
            SafeWrite::discard(staging);
            return {};
        }
    }
    const QString jsonPath = QDir(dir).filePath(profile.id + QStringLiteral(".json"));
    return writeFile(jsonPath, QJsonDocument(o).toJson()) ? profile.id : QString();
}

bool remove(const QString &id)
{
    const QString dir = directory();
    if (dir.isEmpty() || !validId(id) || id == kStandardId) return false;
    QFile::remove(QDir(dir).filePath(id + QStringLiteral(".png")));
    return QFile::remove(QDir(dir).filePath(id + QStringLiteral(".json")));
}

QString selected()
{
    const QString dir = directory();
    QFile file(QDir(dir).filePath(kSelectedFile));
    if (dir.isEmpty() || !file.open(QIODevice::ReadOnly)) return kStandardId;
    const QString id = QString::fromUtf8(file.readAll()).trimmed();
    return validId(id) ? id : kStandardId;
}

void setSelected(const QString &id)
{
    const QString dir = directory();
    if (dir.isEmpty() || !validId(id)) return;
    writeFile(QDir(dir).filePath(kSelectedFile), id.toUtf8());
}

}
