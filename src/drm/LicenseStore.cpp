#include "drm/LicenseStore.hpp"

#include "app/AppConfig.hpp"

#include <QSettings>
#include <QCoreApplication>

namespace {

constexpr auto kUserKey       = "license/businessKey";
constexpr auto kUserUsage     = "license/usage";
constexpr auto kEvalStart     = "license/evaluationStart";

QSettings &userStore()
{
    return AppConfig::store();
}

// Was der Installer hinterlassen hat. Auf Windows die Registry, sonst eine
// Datei am gleichen Ort im Dateisystem-Sinn: systemweit und nur für root
// schreibbar. Fehlt beides, ist es eine private Installation.
QSettings &machineStore()
{
#ifdef Q_OS_WIN
    static QSettings s(QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\OpenPDFStudio"),
                       QSettings::NativeFormat);
#else
    static QSettings s(QStringLiteral("/etc/openpdf-studio/license.conf"),
                       QSettings::IniFormat);
#endif
    return s;
}

} // namespace

namespace License {

QString usage()
{
    const QByteArray override = qgetenv("OPENPDF_USAGE");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override).trimmed().toLower();

    const QString own = userStore().value(QLatin1String(kUserUsage))
                            .toString().trimmed().toLower();
    if (!own.isEmpty())
        return own;

    return machineStore().value(QStringLiteral("Usage")).toString().trimmed().toLower();
}

void setUsage(const QString &usage)
{
    const QString v = usage.trimmed().toLower();
    if (v != QLatin1String("personal") && v != QLatin1String("business"))
        return;

    userStore().setValue(QLatin1String(kUserUsage), v);
    userStore().sync();
}

bool isBusinessInstall()
{
    return usage() == QLatin1String("business");
}

QDate evaluationStart()
{
    if (!isBusinessInstall())
        return {};

    const QDate stored =
        QDate::fromString(userStore().value(QLatin1String(kEvalStart)).toString(),
                          Qt::ISODate);
    if (stored.isValid())
        return stored;

    const QDate today = QDate::currentDate();
    userStore().setValue(QLatin1String(kEvalStart), today.toString(Qt::ISODate));
    userStore().sync();
    return today;
}

int evaluationDaysLeft()
{
    const QDate start = evaluationStart();
    if (!start.isValid())
        return kEvaluationDays;

    // Eine zurückgestellte Uhr darf die Frist nicht verlängern, aber auch nicht
    // in einen negativen Rest kippen.
    const qint64 used = start.daysTo(QDate::currentDate());
    if (used < 0)
        return 0;
    return qMax(0, kEvaluationDays - static_cast<int>(used));
}

bool isEvaluationOver()
{
    return isBusinessInstall() && !hasKey() && evaluationDaysLeft() == 0;
}

QString key()
{
    const QString machine =
        machineStore().value(QStringLiteral("BusinessLicense/Key")).toString().trimmed();
    if (!machine.isEmpty())
        return machine;

    return userStore().value(QLatin1String(kUserKey)).toString().trimmed();
}

bool hasKey()
{
    return !key().isEmpty();
}

bool keyIsMachineWide()
{
    return !machineStore().value(QStringLiteral("BusinessLicense/Key"))
                .toString().trimmed().isEmpty();
}

void setKey(const QString &key)
{
    // Platzhalter: keine Signaturprüfung. Kommt mit modules/rich-media/.
    userStore().setValue(QLatin1String(kUserKey), key.trimmed());
    userStore().sync();
}

void clearKey()
{
    userStore().remove(QLatin1String(kUserKey));
    userStore().sync();
}

} // namespace License
