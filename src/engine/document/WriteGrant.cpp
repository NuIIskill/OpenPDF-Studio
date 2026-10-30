#include "engine/document/WriteGrant.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QUuid>

namespace {

QString scratchFolder()
{
    static const QString folder = QDir(QDir::tempPath()).filePath(
        QStringLiteral("openpdf-scratch-%1").arg(QCoreApplication::applicationPid()));
    QDir().mkpath(folder);
    return folder;
}

}

// Scratch files get a name nobody chose, so they can never be the user's file.
WriteGrant WriteGrant::scratch(const QString &name)
{
    return WriteGrant(QDir(scratchFolder()).filePath(
        QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1Char('-') + name));
}

bool WriteGrant::isScratch(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absolutePath()) == QDir::cleanPath(scratchFolder());
}
