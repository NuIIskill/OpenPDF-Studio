#include "app/AppConfig.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString resolvePath()
{
    const QByteArray env = qgetenv("OPENPDF_CONFIG");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);

    const QString beside = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("config.ini"));
    const QFileInfo fi(beside);
    if (fi.exists() && fi.isWritable())
        return beside;

    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    const QString dir = base + QStringLiteral("/OpenPDF/OpenPDFStudio");
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("config.ini"));
}

}

namespace AppConfig {

QSettings &store()
{
    static QSettings s(resolvePath(), QSettings::IniFormat);
    return s;
}

QString path()
{
    return store().fileName();
}

bool isPortable()
{
    return QFileInfo(path()).absolutePath()
        == QFileInfo(QCoreApplication::applicationDirPath()).absoluteFilePath();
}

}
