#include "app/AppSettings.hpp"

#include "app/AppConfig.hpp"

#include <QSettings>

// ── Helpers ───────────────────────────────────────────────────────────────

static QSettings &settings()
{
    return AppConfig::store();
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

// ── Language ──────────────────────────────────────────────────────────────

QString AppSettings::language() const
{
    return settings().value(QLatin1String(kLanguage), QStringLiteral("en")).toString();
}

void AppSettings::setLanguage(const QString &lang)
{
    settings().setValue(QLatin1String(kLanguage), lang);
}

// ── Shortcuts ─────────────────────────────────────────────────────────────

QKeySequence AppSettings::shortcut(const QString &actionKey,
                                    const QKeySequence &defaultSeq) const
{
    const QString raw = settings().value(QStringLiteral("shortcuts/") + actionKey).toString();
    if (raw.isEmpty()) return defaultSeq;
    const QKeySequence seq = QKeySequence::fromString(raw, QKeySequence::PortableText);
    return seq.isEmpty() ? defaultSeq : seq;
}

void AppSettings::setShortcut(const QString &actionKey, const QKeySequence &seq)
{
    settings().setValue(QStringLiteral("shortcuts/") + actionKey,
                        seq.toString(QKeySequence::PortableText));
}

// ── Zoom ──────────────────────────────────────────────────────────────────

int AppSettings::zoomStep() const
{
    return settings().value(QStringLiteral("zoom/step"), 10).toInt();
}
void AppSettings::setZoomStep(int step)
{
    settings().setValue(QStringLiteral("zoom/step"), step);
}

bool AppSettings::ctrlWheelZoom() const
{
    return settings().value(QStringLiteral("zoom/ctrlWheel"), true).toBool();
}
void AppSettings::setCtrlWheelZoom(bool v)
{
    settings().setValue(QStringLiteral("zoom/ctrlWheel"), v);
}

bool AppSettings::zoomToPointer() const
{
    return settings().value(QStringLiteral("zoom/toPointer"), true).toBool();
}
void AppSettings::setZoomToPointer(bool v)
{
    settings().setValue(QStringLiteral("zoom/toPointer"), v);
}

QString AppSettings::wheelAction() const
{
    return settings().value(QStringLiteral("zoom/wheelAction"),
                            QStringLiteral("scroll")).toString();
}
void AppSettings::setWheelAction(const QString &a)
{
    settings().setValue(QStringLiteral("zoom/wheelAction"), a);
}

// ── Panels ────────────────────────────────────────────────────────────────

bool AppSettings::preservePanelLayout() const
{
    return settings().value(QStringLiteral("panels/preserveLayout"), true).toBool();
}
void AppSettings::setPreservePanelLayout(bool v)
{
    settings().setValue(QStringLiteral("panels/preserveLayout"), v);
}

bool AppSettings::rightPanelCollapsed() const
{
    return settings().value(QStringLiteral("panels/rightCollapsed"), false).toBool();
}
void AppSettings::setRightPanelCollapsed(bool v)
{
    settings().setValue(QStringLiteral("panels/rightCollapsed"), v);
}

QByteArray AppSettings::splitterState() const
{
    return settings().value(QStringLiteral("panels/splitterState")).toByteArray();
}
void AppSettings::setSplitterState(const QByteArray &state)
{
    settings().setValue(QStringLiteral("panels/splitterState"), state);
}

// ── Advanced ──────────────────────────────────────────────────────────────

bool AppSettings::autoUpdateCheck() const
{
    return settings().value(QStringLiteral("advanced/autoUpdateCheck"), true).toBool();
}
void AppSettings::setAutoUpdateCheck(bool v)
{
    settings().setValue(QStringLiteral("advanced/autoUpdateCheck"), v);
}

QString AppSettings::updateInterval() const
{
    return settings().value(QStringLiteral("advanced/updateInterval"),
                            QStringLiteral("daily")).toString();
}
void AppSettings::setUpdateInterval(const QString &interval)
{
    settings().setValue(QStringLiteral("advanced/updateInterval"), interval);
}

QDateTime AppSettings::lastUpdateCheck() const
{
    return QDateTime::fromString(
        settings().value(QStringLiteral("advanced/lastUpdateCheck")).toString(),
        Qt::ISODate);
}
void AppSettings::setLastUpdateCheck(const QDateTime &when)
{
    settings().setValue(QStringLiteral("advanced/lastUpdateCheck"),
                        when.toString(Qt::ISODate));
}

bool AppSettings::hardwareAcceleration() const
{
    return settings().value(QStringLiteral("advanced/hardwareAcceleration"), false).toBool();
}
void AppSettings::setHardwareAcceleration(bool v)
{
    settings().setValue(QStringLiteral("advanced/hardwareAcceleration"), v);
}

bool AppSettings::limitMemoryUsage() const
{
    return settings().value(QStringLiteral("advanced/limitMemoryUsage"), false).toBool();
}
void AppSettings::setLimitMemoryUsage(bool v)
{
    settings().setValue(QStringLiteral("advanced/limitMemoryUsage"), v);
}

bool AppSettings::debugLogging() const
{
    return settings().value(QStringLiteral("advanced/debugLogging"), false).toBool();
}
void AppSettings::setDebugLogging(bool v)
{
    settings().setValue(QStringLiteral("advanced/debugLogging"), v);
}

QString AppSettings::logLevel() const
{
    return settings().value(QStringLiteral("advanced/logLevel"),
                            QStringLiteral("info")).toString();
}
void AppSettings::setLogLevel(const QString &level)
{
    settings().setValue(QStringLiteral("advanced/logLevel"), level);
}

// ── Sync ──────────────────────────────────────────────────────────────────

void AppSettings::sync()
{
    settings().sync();
}
