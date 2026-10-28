// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/engine/MediaConvert.hpp"

#include "rich-media/engine/MediaFormat.hpp"
#include "rich-media/engine/SystemEncoder.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QProcess>
#include <QStandardPaths>

namespace {

QString locate(const char *name)
{
    const QString tool = QLatin1String(name);
    const QDir app(QCoreApplication::applicationDirPath());

#ifdef Q_OS_WIN
    // Only the one shipped with the program or fetched on Settings > Add-on
    // Packages, so one the user removed there stays unused.
    return QStandardPaths::findExecutable(tool, {
        app.absolutePath(), QFileInfo(MediaConvert::downloadedPath()).absolutePath() });
#else
    const QStringList nearby {
        app.absolutePath(),
        app.absoluteFilePath(QStringLiteral("ffmpeg")),
        app.absoluteFilePath(QStringLiteral("ffmpeg/bin")),
        app.absoluteFilePath(QStringLiteral("../ffmpeg/bin")),
    };
    const QString found = QStandardPaths::findExecutable(tool, nearby);
    if (!found.isEmpty()) return found;
    return QStandardPaths::findExecutable(tool);
#endif
}

bool needsReencoding(const MediaFormat::Info &info, int maxHeight)
{
    return !info.videoIsH264() || (maxHeight > 0 && info.size.height() > maxHeight);
}

// The H.264 encoder this ffmpeg carries, empty when it has none.
QString h264Encoder(const QString &ffmpeg)
{
    if (ffmpeg.isEmpty()) return {};

    static QMutex lock;
    static QHash<QString, QString> known;
    const QMutexLocker guard(&lock);
    const auto it = known.constFind(ffmpeg);
    if (it != known.cend()) return *it;

#ifdef Q_OS_WIN
    static const char *const kWanted[] = { "h264_mf" };
#else
    // Fedora's own ffmpeg carries only OpenH264; x264 comes from RPM Fusion.
    static const char *const kWanted[] = { "libx264", "libopenh264" };
#endif
    QProcess process;
    process.start(ffmpeg, { QStringLiteral("-hide_banner"), QStringLiteral("-encoders") });
    QString found;
    if (process.waitForFinished(5000)) {
        const QByteArray list = process.readAllStandardOutput();
        for (const char *name : kWanted)
            if (list.contains(' ' + QByteArray(name) + ' ')) {
                found = QLatin1String(name);
                break;
            }
    }
    known.insert(ffmpeg, found);
    return found;
}

bool runFfmpeg(const QString &ffmpeg, const QString &encoder, const QString &in,
               const QString &out, const MediaFormat::Info &info,
               const std::function<bool(int)> &progress, int maxHeight)
{
    QStringList arguments {
        QStringLiteral("-nostdin"),
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-i"), in,
    };
    const bool scaling = maxHeight > 0 && info.size.height() > maxHeight;
    if (!needsReencoding(info, maxHeight)) {

        arguments << QStringLiteral("-c:v") << QStringLiteral("copy");
    } else {
#ifdef Q_OS_WIN
        // Windows' own H.264 encoder: the ffmpeg shipped with the program
        // carries no x264 and stays LGPL.
        arguments << QStringLiteral("-c:v")          << encoder
                  << QStringLiteral("-rate_control") << QStringLiteral("quality")
                  << QStringLiteral("-quality")      << QStringLiteral("60")
                  << QStringLiteral("-profile:v")    << QStringLiteral("100")
                  << QStringLiteral("-pix_fmt")      << QStringLiteral("nv12");
#else
        if (encoder == QLatin1String("libx264")) {
            arguments << QStringLiteral("-c:v")       << QStringLiteral("libx264")
                      << QStringLiteral("-preset")    << QStringLiteral("veryfast")
                      << QStringLiteral("-crf")       << QStringLiteral("22")
                      << QStringLiteral("-profile:v") << QStringLiteral("high")
                      << QStringLiteral("-level")     << QStringLiteral("4.1")

                      << QStringLiteral("-pix_fmt")   << QStringLiteral("yuv420p");
        } else {
            // OpenH264 knows no quality setting, only a bit rate: about three
            // bits per pixel and second, 6 Mbit/s for 1080p.
            const int height = scaling ? maxHeight : info.size.height();
            const qint64 width = info.size.height() > 0
                ? qint64(info.size.width()) * height / info.size.height() : 0;
            const qint64 rate = qBound(qint64(1'000'000), width * height * 3,
                                       qint64(12'000'000));
            arguments << QStringLiteral("-c:v")     << encoder
                      << QStringLiteral("-b:v")     << QString::number(rate)
                      << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
        }
#endif
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

}

QString MediaConvert::bundledPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/ffmpeg.exe");
}

QString MediaConvert::downloadedPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
         + QStringLiteral("/ffmpeg/ffmpeg.exe");
}

QString MediaConvert::toolPath()
{
    // Looked up each time, so an ffmpeg installed or removed meanwhile counts.
    return locate("ffmpeg");
}

bool MediaConvert::available()
{
    return SystemEncoder::available() || !h264Encoder(toolPath()).isEmpty();
}

bool MediaConvert::run(const QString &in, const QString &out,
                       const std::function<bool(int)> &progress, int maxHeight)
{
    if (in.isEmpty() || out.isEmpty()) return false;

    const MediaFormat::Info info = MediaFormat::inspect(in);
    const QString ffmpeg = toolPath();

    // ffmpeg can move an H.264 stream into MP4 without encoding it again.
    const bool repackage = !ffmpeg.isEmpty() && !needsReencoding(info, maxHeight);
    if (!repackage && SystemEncoder::available()) {
        switch (SystemEncoder::run(in, out, progress, maxHeight)) {
        case SystemEncoder::Result::Done:      return true;
        case SystemEncoder::Result::Cancelled: return false;
        case SystemEncoder::Result::Failed:    break;
        }
    }
    if (ffmpeg.isEmpty()) return false;
    const QString encoder = h264Encoder(ffmpeg);
    if (!repackage && encoder.isEmpty()) return false;
    return runFfmpeg(ffmpeg, encoder, in, out, info, progress, maxHeight);
}
