// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
//
// Grabbing a still with Qt Multimedia. Used everywhere except Windows.

#include "rich-media/engine/VideoStill.hpp"

#ifndef Q_OS_WIN

#include <QAudioOutput>
#include <QEventLoop>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

namespace {

/// How black a frame may be before it is skipped. Videos very often open on a
/// black frame, and a black poster reads as a failure.
bool tooDark(const QImage &image)
{
    if (image.isNull()) return true;
    const QImage small = image.scaled(24, 24, Qt::IgnoreAspectRatio,
                                      Qt::FastTransformation);
    qint64 sum = 0;
    for (int y = 0; y < small.height(); ++y)
        for (int x = 0; x < small.width(); ++x)
            sum += qGray(small.pixel(x, y));
    return int(sum / qMax(1, small.width() * small.height())) < 12;
}

} // namespace

QImage VideoStill::grab(const QString &filePath, int maxWidth)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) return {};

    QMediaPlayer player;
    QVideoSink   sink;
    QAudioOutput audio;
    audio.setMuted(true);
    player.setAudioOutput(&audio);
    player.setVideoSink(&sink);

    QImage best;
    int    seen = 0;
    QEventLoop loop;

    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &loop,
                     [&](const QVideoFrame &frame) {
        if (!frame.isValid()) return;
        const QImage image = frame.toImage();
        if (image.isNull()) return;
        ++seen;
        if (best.isNull() || (tooDark(best) && !tooDark(image)))
            best = image.copy();
        if (!tooDark(best) || seen > 20) loop.quit();
    });
    QObject::connect(&player, &QMediaPlayer::errorOccurred, &loop,
                     [&](QMediaPlayer::Error, const QString &) { loop.quit(); });
    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &loop,
                     [&](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia
            || status == QMediaPlayer::InvalidMedia) loop.quit();
    });

    // A still takes a fraction of a second. Longer than this and something is
    // wrong; the caller draws a placeholder instead of waiting.
    QTimer::singleShot(6000, &loop, &QEventLoop::quit);

    player.setSource(QUrl::fromLocalFile(filePath));
    player.play();
    loop.exec();
    player.stop();

    if (best.isNull()) return {};
    if (maxWidth > 0 && best.width() > maxWidth)
        best = best.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    return best;
}

#endif // !Q_OS_WIN
