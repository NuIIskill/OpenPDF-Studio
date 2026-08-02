#include "SessionStore.hpp"

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

QString newWorkingFile(const QString &sourcePath)
{
    const QString dir = directory();
    if (dir.isEmpty()) return {};

    QString stem = QFileInfo(sourcePath).completeBaseName();
    if (stem.isEmpty()) stem = QStringLiteral("untitled");
    // Keep the name recognisable in a recovery listing, but strip anything that
    // could escape the directory or upset a filesystem.
    stem.replace(QRegularExpression(QStringLiteral("[^\\w.-]")), QStringLiteral("_"));
    stem.truncate(64);

    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    const qint64 pid = QCoreApplication::applicationPid();

    // A second save within the same session must not reuse the file the view is
    // currently reading from, so disambiguate until a free name turns up.
    for (int n = 0; n < 1000; ++n) {
        const QString suffix = n == 0 ? QString() : QStringLiteral("-%1").arg(n);
        const QString path = QStringLiteral("%1/%2-%3-%4%5.pdf")
                                 .arg(dir, stem, stamp, QString::number(pid), suffix);
        if (!QFile::exists(path)) return path;
    }
    return {};
}

bool isWorkingFile(const QString &path)
{
    if (path.isEmpty()) return false;
    const QString dir = directory();
    if (dir.isEmpty()) return false;
    return QFileInfo(path).absolutePath() == dir;
}

void discard(const QString &path)
{
    if (isWorkingFile(path)) QFile::remove(path);
}

} // namespace SessionStore
