// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/engine/MediaConvert.hpp"

#include "rich-media/engine/MediaFormat.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {

QString locate(const char *name)
{
    const QString tool = QLatin1String(name);

    const QString onPath = QStandardPaths::findExecutable(tool);
    if (!onPath.isEmpty()) return onPath;

    const QDir app(QCoreApplication::applicationDirPath());
    const QStringList nearby {
        app.absolutePath(),
        app.absoluteFilePath(QStringLiteral("ffmpeg")),
        app.absoluteFilePath(QStringLiteral("ffmpeg/bin")),
        app.absoluteFilePath(QStringLiteral("../ffmpeg/bin")),
    };
    const QString found = QStandardPaths::findExecutable(tool, nearby);
    if (!found.isEmpty()) return found;

#ifdef Q_OS_WIN

    static const QStringList kInstalled {
        QStringLiteral("C:/Program Files/ffmpeg/bin"),
        QStringLiteral("C:/ffmpeg/bin"),
        QStringLiteral("C:/ProgramData/chocolatey/bin"),
    };
    return QStandardPaths::findExecutable(tool, kInstalled);
#else
    return {};
#endif
}

}

QString MediaConvert::toolPath()
{
    static const QString path = locate("ffmpeg");
    return path;
}

bool MediaConvert::available()
{
    return !toolPath().isEmpty();
}

bool MediaConvert::run(const QString &in, const QString &out,
                       const std::function<bool(int)> &progress, int maxHeight)
{
    const QString ffmpeg = toolPath();
    if (ffmpeg.isEmpty() || in.isEmpty() || out.isEmpty()) return false;

    const MediaFormat::Info info = MediaFormat::inspect(in);

    QStringList arguments {
        QStringLiteral("-nostdin"),
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-i"), in,
    };
    const bool scaling = maxHeight > 0 && info.size.height() > maxHeight;
    if (info.videoIsH264() && !scaling) {

        arguments << QStringLiteral("-c:v") << QStringLiteral("copy");
    } else {
        arguments << QStringLiteral("-c:v")       << QStringLiteral("libx264")
                  << QStringLiteral("-preset")    << QStringLiteral("veryfast")
                  << QStringLiteral("-crf")       << QStringLiteral("22")
                  << QStringLiteral("-profile:v") << QStringLiteral("high")
                  << QStringLiteral("-level")     << QStringLiteral("4.1")

                  << QStringLiteral("-pix_fmt")   << QStringLiteral("yuv420p");
        if (scaling) {

            arguments << QStringLiteral("-vf")
                      << QStringLiteral("scale=-2:%1").arg(maxHeight);
        }
    }
    arguments << QStringLiteral("-c:a") << QStringLiteral("aac")
              << QStringLiteral("-b:a") << QStringLiteral("160k")

              << QStringLiteral("-movflags") << QStringLiteral("+faststart")
              << QStringLiteral("-progress")  << QStringLiteral("pipe:1")
              << QStringLiteral("-nostats")
              << QStringLiteral("-y") << out;

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(ffmpeg, arguments);
    if (!process.waitForStarted(5000)) return false;

    const double totalUs = info.durationSec * 1'000'000.0;
    bool cancelled = false;

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(200);
        const QByteArray chunk = process.readAllStandardOutput();
        for (const QByteArray &line : chunk.split('\n')) {
            if (!line.startsWith("out_time_us=")) continue;
            const double doneUs = line.mid(12).trimmed().toDouble();
            const int percent = totalUs > 0.0
                ? qBound(0, static_cast<int>(doneUs / totalUs * 100.0), 100) : 0;
            if (progress && !progress(percent)) { cancelled = true; break; }
        }
        if (cancelled) break;
    }

    if (cancelled) {
        process.kill();
        process.waitForFinished(2000);
        QFile::remove(out);
        return false;
    }

    process.waitForFinished(-1);
    const bool ok = process.exitStatus() == QProcess::NormalExit
                 && process.exitCode() == 0 && QFileInfo(out).size() > 0;
    if (!ok) {
        qWarning() << "[rich-media] conversion failed:"
                   << process.readAllStandardError();
        QFile::remove(out);
    }
    return ok;
}
