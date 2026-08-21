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

    // Portabel: nur übernehmen, wenn die Datei schon da ist *und* sich
    // beschreiben lässt. Eine Installation unter Program Files fällt damit von
    // selbst auf das Benutzerprofil zurück, statt Einstellungen zu verlieren.
    const QString beside = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("config.ini"));
    const QFileInfo fi(beside);
    if (fi.exists() && fi.isWritable())
        return beside;

    // Bewusst aus GenericConfigLocation zusammengesetzt statt aus
    // AppConfigLocation: letzteres hängt am Anwendungsnamen ("OpenPDF Studio"),
    // und ein Leerzeichen im Pfad ist für eine Datei, die man dem Nutzer nennt,
    // keine gute Idee.
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    const QString dir = base + QStringLiteral("/OpenPDF/OpenPDFStudio");
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("config.ini"));
}

// Einmalige Übernahme aus dem alten Speicherort: Registry auf Windows,
// ~/.config/OpenPDF/OpenPDFStudio.conf auf Linux. Ohne das stünde nach einem
// Update jeder wieder bei den Vorgaben — Theme, Kürzel, Fensterlage weg.
void migrateLegacy(QSettings &target)
{
    if (!target.allKeys().isEmpty())
        return;

    QSettings legacy(QStringLiteral("OpenPDF"), QStringLiteral("OpenPDFStudio"));
    const QStringList keys = legacy.allKeys();
    if (keys.isEmpty())
        return;

    for (const QString &k : keys)
        target.setValue(k, legacy.value(k));
    target.sync();
}

} // namespace

namespace AppConfig {

QSettings &store()
{
    static QSettings s(resolvePath(), QSettings::IniFormat);
    static const bool migrated = [] { migrateLegacy(s); return true; }();
    Q_UNUSED(migrated);
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

} // namespace AppConfig
