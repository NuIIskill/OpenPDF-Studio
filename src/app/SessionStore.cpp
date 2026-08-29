#include "app/SessionStore.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace SessionStore {

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

}
