#include "app/SignatureStore.hpp"
#include "app/AppConfig.hpp"
#include "app/SafeWrite.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>

#include <algorithm>

namespace SignatureStore {

namespace {

const QString kNameKey    = QStringLiteral("Title");
const QString kCreatedKey = QStringLiteral("Created");

QString directory()
{
    const QDir dir(QFileInfo(AppConfig::path()).absoluteDir().filePath(QStringLiteral("signatures")));
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) return {};
    return dir.absolutePath();
}

bool inStore(const QString &path)
{
    const QString dir = directory();
    return !dir.isEmpty() && QFileInfo(path).absolutePath() == dir;
}

bool write(QImage image, const QString &target, const QString &name, const QDateTime &created)
{
    image.setText(kNameKey, name);
    image.setText(kCreatedKey, created.toString(Qt::ISODateWithMs));
    const QString staging = SafeWrite::stagingPath(target);
    if (image.save(staging, "PNG") && SafeWrite::commit(staging, target)) return true;
    SafeWrite::discard(staging);
    return false;
}

}

QList<Entry> list()
{
    const QString dir = directory();
    if (dir.isEmpty()) return {};

    QList<Entry> entries;
    const QFileInfoList files = QDir(dir).entryInfoList({ QStringLiteral("*.png") }, QDir::Files);
    for (const QFileInfo &fi : files) {
        QImageReader reader(fi.absoluteFilePath());
        QDateTime created = QDateTime::fromString(reader.text(kCreatedKey), Qt::ISODateWithMs);
        if (!created.isValid()) created = fi.lastModified();
        entries.append({ fi.absoluteFilePath(), reader.text(kNameKey), created });
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) {
                  return a.created != b.created ? a.created > b.created : a.path > b.path;
              });
    return entries;
}

QString save(const QImage &image, const QString &name)
{
    const QString dir = directory();
    if (image.isNull() || dir.isEmpty()) return {};

    const QDateTime now = QDateTime::currentDateTime();
    const QString target = QDir(dir).filePath(
        QStringLiteral("signature-%1.png").arg(now.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))));
    return write(image, target, name, now) ? target : QString();
}

bool rename(const QString &path, const QString &name)
{
    if (!inStore(path)) return false;
    QDateTime created;
    QImage image;
    {
        QImageReader reader(path);
        created = QDateTime::fromString(reader.text(kCreatedKey), Qt::ISODateWithMs);
        image = reader.read();
    }
    if (!created.isValid()) created = QFileInfo(path).lastModified();
    return !image.isNull() && write(image, path, name, created);
}

bool remove(const QString &path)
{
    return inStore(path) && QFile::remove(path);
}

}
