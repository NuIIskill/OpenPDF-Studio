#include "SafeWrite.hpp"

#include <QDebug>
#include <QFile>
#include <QFileInfo>

namespace SafeWrite {

QString stagingPath(const QString &target)
{
    if (target.isEmpty()) return {};
    for (int n = 0; n < 100; ++n) {
        const QString path = n == 0
            ? target + QStringLiteral(".opdf-save")
            : target + QStringLiteral(".opdf-save%1").arg(n);
        if (!QFile::exists(path)) return path;
    }
    return {};
}

bool commit(const QString &stagingPath, const QString &target)
{
    if (stagingPath.isEmpty() || target.isEmpty()) return false;
    if (!QFileInfo::exists(stagingPath) || QFileInfo(stagingPath).size() == 0) {
        qWarning() << "SafeWrite: nothing was written to" << stagingPath;
        discard(stagingPath);
        return false;
    }
    if (!QFile::exists(target))
        return QFile::rename(stagingPath, target);

    // QFile::rename does not replace an existing file, so the old document is
    // moved aside first and kept until the new one is safely in place.
    const QString backup = target + QStringLiteral(".opdf-old");
    QFile::remove(backup);
    if (!QFile::rename(target, backup)) {
        qWarning() << "SafeWrite: cannot move" << target << "aside";
        discard(stagingPath);
        return false;
    }
    if (!QFile::rename(stagingPath, target)) {
        qWarning() << "SafeWrite: cannot put" << stagingPath << "in place —"
                   << "restoring the previous file";
        QFile::rename(backup, target);
        discard(stagingPath);
        return false;
    }
    QFile::remove(backup);
    return true;
}

void discard(const QString &stagingPath)
{
    if (!stagingPath.isEmpty()) QFile::remove(stagingPath);
}

} // namespace SafeWrite
