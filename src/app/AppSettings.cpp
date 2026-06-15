#include "AppSettings.hpp"

#include <QSettings>

// ── Helpers ───────────────────────────────────────────────────────────────

static QSettings &settings()
{
    static QSettings s(QStringLiteral("OpenPDF"), QStringLiteral("OpenPDFStudio"));
    return s;
}

// ── AppSettings ───────────────────────────────────────────────────────────

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{}

// ── Window ────────────────────────────────────────────────────────────────

QByteArray AppSettings::windowGeometry() const
{
    return settings().value(QLatin1String(kWindowGeometry)).toByteArray();
}

void AppSettings::setWindowGeometry(const QByteArray &geometry)
{
    settings().setValue(QLatin1String(kWindowGeometry), geometry);
}

QByteArray AppSettings::windowState() const
{
    return settings().value(QLatin1String(kWindowState)).toByteArray();
}

void AppSettings::setWindowState(const QByteArray &state)
{
    settings().setValue(QLatin1String(kWindowState), state);
}

// ── Document ──────────────────────────────────────────────────────────────

QString AppSettings::lastOpenedFile() const
{
    return settings().value(QLatin1String(kLastOpenedFile)).toString();
}

void AppSettings::setLastOpenedFile(const QString &path)
{
    settings().setValue(QLatin1String(kLastOpenedFile), path);
}

// ── View ──────────────────────────────────────────────────────────────────

int AppSettings::zoomLevel() const
{
    return settings().value(QLatin1String(kZoomLevel), 100).toInt();
}

void AppSettings::setZoomLevel(int percent)
{
    settings().setValue(QLatin1String(kZoomLevel), percent);
}

// ── Appearance ────────────────────────────────────────────────────────────

QString AppSettings::theme() const
{
    return settings().value(QLatin1String(kTheme), QStringLiteral("system")).toString();
}

void AppSettings::setTheme(const QString &name)
{
    settings().setValue(QLatin1String(kTheme), name);
}

// ── Sync ──────────────────────────────────────────────────────────────────

void AppSettings::sync()
{
    settings().sync();
}
