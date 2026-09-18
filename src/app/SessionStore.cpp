#include "app/SessionStore.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QStandardPaths>

#include <memory>

namespace SessionStore {

namespace {

std::unique_ptr<QLockFile> g_lock;
QString    g_manifest;
QByteArray g_written;

QString liveDirectory()
{
    const QString base = directory();
    if (base.isEmpty()) return {};

    QDir dir(base + QStringLiteral("/live"));
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) return {};
    return dir.absolutePath();
}

QJsonObject toJson(const OpenDocument &doc)
{
    QJsonObject o;
    o[QStringLiteral("target")]  = doc.target;
    o[QStringLiteral("content")] = doc.content;
    o[QStringLiteral("page")]    = doc.page;
    o[QStringLiteral("dirty")]   = doc.dirty;
    return o;
}

OpenDocument fromJson(const QJsonObject &o)
{
    OpenDocument doc;
    doc.target  = o.value(QStringLiteral("target")).toString();
    doc.content = o.value(QStringLiteral("content")).toString();
    doc.page    = o.value(QStringLiteral("page")).toInt();
    doc.dirty   = o.value(QStringLiteral("dirty")).toBool();
    return doc;
}

}

QString directory()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) return {};

    QDir dir(base + QStringLiteral("/session"));
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) return {};
    return dir.absolutePath();
}

QString snapshotDirectory()
{
    const QString base = directory();
    if (base.isEmpty()) return {};

    QDir dir(base + QStringLiteral("/history"));
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) return {};
    return dir.absolutePath();
}

static QString allocateIn(const QString &dir, const QString &sourcePath)
{
    if (dir.isEmpty()) return {};

    QString stem = QFileInfo(sourcePath).completeBaseName();
    if (stem.isEmpty()) stem = QStringLiteral("untitled");

    stem.replace(QRegularExpression(QStringLiteral("[^\\w.-]")), QStringLiteral("_"));
    stem.truncate(64);

    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    const qint64 pid = QCoreApplication::applicationPid();

    for (int n = 0; n < 1000; ++n) {
        const QString suffix = n == 0 ? QString() : QStringLiteral("-%1").arg(n);
        const QString path = QStringLiteral("%1/%2-%3-%4%5.pdf")
                                 .arg(dir, stem, stamp, QString::number(pid), suffix);
        if (!QFile::exists(path)) return path;
    }
    return {};
}

QString newWorkingFile(const QString &sourcePath)
{
    return allocateIn(directory(), sourcePath);
}

QString newSnapshotFile(const QString &sourcePath)
{
    return allocateIn(snapshotDirectory(), sourcePath);
}

static bool isIn(const QString &dir, const QString &path)
{
    if (path.isEmpty() || dir.isEmpty()) return false;
    return QFileInfo(path).absolutePath() == dir;
}

bool isWorkingFile(const QString &path)
{
    return isIn(directory(), path);
}

bool isSnapshotFile(const QString &path)
{
    return isIn(snapshotDirectory(), path);
}

void discard(const QString &path)
{
    if (isWorkingFile(path)) QFile::remove(path);
}

void discardSnapshot(const QString &path)
{
    if (isSnapshotFile(path)) QFile::remove(path);
}

void pruneSnapshots(int maxAgeDays)
{
    const QString dir = snapshotDirectory();
    if (dir.isEmpty()) return;

    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-maxAgeDays);
    const QFileInfoList files =
        QDir(dir).entryInfoList(QDir::Files | QDir::NoSymLinks);
    for (const QFileInfo &fi : files)
        if (fi.lastModified() < cutoff) QFile::remove(fi.absoluteFilePath());
}

bool beginSession()
{
    if (g_lock) return true;

    const QString dir = liveDirectory();
    if (dir.isEmpty()) return false;

    const QString id =
        QStringLiteral("%1-%2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss")),
                 QString::number(QCoreApplication::applicationPid()));

    auto lock = std::make_unique<QLockFile>(dir + QStringLiteral("/") + id
                                            + QStringLiteral(".lock"));
    lock->setStaleLockTime(1);
    if (!lock->tryLock(0)) return false;

    g_lock     = std::move(lock);
    g_manifest = dir + QStringLiteral("/") + id + QStringLiteral(".json");
    g_written.clear();
    return true;
}

void updateSession(const QList<OpenDocument> &documents)
{
    if (!g_lock || g_manifest.isEmpty()) return;

    QJsonArray array;
    for (const OpenDocument &doc : documents) array.append(toJson(doc));

    QJsonObject root;
    root[QStringLiteral("documents")] = array;

    const QByteArray json =
        QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (json == g_written) return;

    QFile file(g_manifest);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    if (file.write(json) != json.size() || !file.flush()) return;
    file.close();
    g_written = json;
}

void endSession()
{
    if (!g_manifest.isEmpty()) QFile::remove(g_manifest);
    g_manifest.clear();
    g_written.clear();
    g_lock.reset();
}

QList<OpenDocument> takeAbandonedDocuments()
{
    QList<OpenDocument> out;

    const QString dir = liveDirectory();
    if (dir.isEmpty()) return out;

    const QFileInfoList manifests = QDir(dir).entryInfoList(
        { QStringLiteral("*.json") }, QDir::Files | QDir::NoSymLinks, QDir::Time);

    for (const QFileInfo &fi : manifests) {
        if (!g_manifest.isEmpty() && fi.absoluteFilePath() == g_manifest) continue;

        QLockFile lock(dir + QStringLiteral("/") + fi.completeBaseName()
                       + QStringLiteral(".lock"));
        lock.setStaleLockTime(1);
        if (!lock.tryLock(0)) continue;

        QFile file(fi.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonArray documents =
                QJsonDocument::fromJson(file.readAll())
                    .object()
                    .value(QStringLiteral("documents"))
                    .toArray();
            file.close();

            for (const QJsonValue &value : documents) {
                OpenDocument doc = fromJson(value.toObject());
                if (!doc.content.isEmpty()
                        && (!isWorkingFile(doc.content)
                            || !QFile::exists(doc.content)))
                    doc.content.clear();
                if (doc.content.isEmpty()
                        && (doc.target.isEmpty() || !QFile::exists(doc.target)))
                    continue;
                out.append(doc);
            }
        }

        QFile::remove(fi.absoluteFilePath());
        lock.unlock();
    }

    const QFileInfoList locks = QDir(dir).entryInfoList(
        { QStringLiteral("*.lock") }, QDir::Files | QDir::NoSymLinks);
    for (const QFileInfo &fi : locks) {
        QLockFile lock(fi.absoluteFilePath());
        lock.setStaleLockTime(1);
        if (lock.tryLock(0)) lock.unlock();
    }
    return out;
}

}
