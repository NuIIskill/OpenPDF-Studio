// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/engine/ExternalPlayer.hpp"

#include <QDebug>
#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUrl>

namespace {

QString g_lastPlayer;

QProcessEnvironment cleanEnvironment()
{
    const QProcessEnvironment inherited = QProcessEnvironment::systemEnvironment();
    QProcessEnvironment clean;
    const QStringList keys = inherited.keys();
    for (const QString &key : keys) {
        if (key.startsWith(QLatin1String("QT_"))
            || key == QLatin1String("LD_LIBRARY_PATH")
            || key == QLatin1String("LD_PRELOAD")
            || key == QLatin1String("QML2_IMPORT_PATH")
            || key == QLatin1String("APPDIR")
            || key == QLatin1String("APPIMAGE"))
            continue;
        clean.insert(key, inherited.value(key));
    }
    return clean;
}

QString locate(const QString &program)
{
    const QString onPath = QStandardPaths::findExecutable(program);
    if (!onPath.isEmpty()) return onPath;

#ifdef Q_OS_WIN
    static const QStringList kPlaces {
        QStringLiteral("C:/Program Files/VideoLAN/VLC"),
        QStringLiteral("C:/Program Files (x86)/VideoLAN/VLC"),
        QStringLiteral("C:/Program Files/mpv"),
        QStringLiteral("C:/ProgramData/chocolatey/bin"),
    };
    return QStandardPaths::findExecutable(program, kPlaces);
#else
    return {};
#endif
}

bool start(const QString &program, const QStringList &arguments)
{
    const QString executable = locate(program);
    if (executable.isEmpty()) return false;
    QProcess player;
    player.setProgram(executable);
    player.setArguments(arguments);
    player.setProcessEnvironment(cleanEnvironment());
    if (!player.startDetached()) return false;
    g_lastPlayer = program;
    return true;
}

QStringList splitCommand(const QString &command)
{
    return QProcess::splitCommand(command);
}

}

QString ExternalPlayer::lastPlayerName()
{
    return g_lastPlayer;
}

bool ExternalPlayer::play(const QString &filePath, const QString &command)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) return false;

    if (!command.trimmed().isEmpty()) {
        QStringList parts = splitCommand(command.trimmed());
        if (!parts.isEmpty()) {
            const QString program = parts.takeFirst();
            bool substituted = false;
            for (QString &arg : parts) {
                if (!arg.contains(QLatin1String("%1"))) continue;
                arg.replace(QLatin1String("%1"), filePath);
                substituted = true;
            }
            if (!substituted) parts << filePath;
            if (start(program, parts)) return true;
            qWarning() << "[rich-media] custom player would not start:" << program;
        }
    }

    struct Candidate { const char *program; QStringList args; };
    const Candidate candidates[] = {
        { "vlc",     { QStringLiteral("--play-and-exit"), filePath } },
        { "mpv",     { QStringLiteral("--force-window=yes"), filePath } },
        { "mplayer", { filePath } },
    };
    for (const Candidate &c : candidates)
        if (start(QLatin1String(c.program), c.args)) return true;

    if (QDesktopServices::openUrl(QUrl::fromLocalFile(filePath))) {
        g_lastPlayer = QStringLiteral("system");
        return true;
    }

    g_lastPlayer.clear();
    return false;
}
