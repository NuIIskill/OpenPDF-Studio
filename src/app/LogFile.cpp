#include "app/LogFile.hpp"

#include "app/AppConfig.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutex>

namespace {

constexpr qint64 kMaxBytes = 5 * 1024 * 1024;

QMutex           g_mutex;
QFile           *g_file     = nullptr;
int              g_minimum  = 1;
QtMessageHandler g_previous = nullptr;

int rank(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return 0;
    case QtInfoMsg:     return 1;
    case QtWarningMsg:  return 2;
    case QtCriticalMsg: return 3;
    case QtFatalMsg:    return 4;
    }
    return 4;
}

int rankOf(const QString &level)
{
    if (level == QLatin1String("debug"))   return 0;
    if (level == QLatin1String("warning")) return 2;
    if (level == QLatin1String("error"))   return 3;
    return 1;
}

const char *nameOf(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "debug";
    case QtInfoMsg:     return "info";
    case QtWarningMsg:  return "warning";
    case QtCriticalMsg: return "error";
    case QtFatalMsg:    return "fatal";
    }
    return "?";
}

void writeLine(const QString &line)
{
    g_file->write(line.toUtf8());
    g_file->write("\n");
    g_file->flush();
}

void handle(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    {
        QMutexLocker lock(&g_mutex);
        if (g_file && rank(type) >= g_minimum)
            writeLine(QStringLiteral("%1 %2 %3").arg(
                QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                QLatin1String(nameOf(type)), message));
    }
    if (g_previous) g_previous(type, context, message);
}

}

namespace LogFile {

void configure(bool enabled, const QString &level)
{
    // Some distributions switch qDebug off in a system-wide qtlogging.ini.
    QLoggingCategory::setFilterRules(enabled && rankOf(level) == 0
        ? QStringLiteral("default.debug=true") : QString());

    QMutexLocker lock(&g_mutex);
    g_minimum = rankOf(level);

    if (!enabled) {
        delete g_file;
        g_file = nullptr;
        return;
    }

    if (!g_file) {
        const QString target = path();
        if (QFileInfo(target).size() > kMaxBytes) {
            QFile::remove(target + QStringLiteral(".1"));
            QFile::rename(target, target + QStringLiteral(".1"));
        }
        auto *file = new QFile(target);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            delete file;
            return;
        }
        g_file = file;
        writeLine(QStringLiteral("--- %1 %2, %3 ---").arg(
            QCoreApplication::applicationName(),
            QCoreApplication::applicationVersion(),
            QDateTime::currentDateTime().toString(Qt::ISODate)));
    }

    if (!g_previous) g_previous = qInstallMessageHandler(handle);
}

QString path()
{
    return QFileInfo(AppConfig::path()).dir().filePath(QStringLiteral("openpdf-studio.log"));
}

}
