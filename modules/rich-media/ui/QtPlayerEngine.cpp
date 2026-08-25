// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
//
// The Qt Multimedia engine. Used everywhere except Windows.

#include "rich-media/ui/PlayerEngine.hpp"

#ifndef Q_OS_WIN

#include <QAudioOutput>
#include <QCoreApplication>
#include <QDir>
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

namespace {

class QtPlayerEngine : public PlayerEngine
{
public:
    explicit QtPlayerEngine(QObject *parent)
        : PlayerEngine(parent)
        , m_player(new QMediaPlayer(this))
        , m_audio(new QAudioOutput(this))
        , m_sink(new QVideoSink(this))
    {
        m_player->setAudioOutput(m_audio);
        m_player->setVideoSink(m_sink);

        // Converted here, once per frame, and not while painting: paints
        // happen more often than frames, and a hardware-backed frame is
        // mapped and read back on every one of them.
        connect(m_sink, &QVideoSink::videoFrameChanged, this,
                [this](const QVideoFrame &frame) {
            if (!frame.isValid()) return;
            const QImage image = frame.toImage();
            if (image.isNull()) return;
            m_image = image;
            Q_EMIT imageChanged();
        });
        connect(m_player, &QMediaPlayer::positionChanged,
                this, &PlayerEngine::positionChanged);
        connect(m_player, &QMediaPlayer::durationChanged,
                this, &PlayerEngine::durationChanged);
        connect(m_player, &QMediaPlayer::playbackStateChanged,
                this, [this](QMediaPlayer::PlaybackState) { Q_EMIT playingChanged(); });
        connect(m_player, &QMediaPlayer::errorOccurred, this,
                [this](QMediaPlayer::Error, const QString &message) {
            Q_EMIT failed(message);
        });
    }

    ~QtPlayerEngine() override
    {
        // No more frames, then stop, then let go of source and sink: a decoder
        // still running while its target falls apart takes the process along.
        disconnect(m_sink, nullptr, this, nullptr);
        m_player->stop();
        m_player->setSource(QUrl());
        m_player->setVideoSink(nullptr);
    }

    void play(const QString &filePath, bool loop, bool muted) override
    {
        m_audio->setMuted(muted);
        m_player->setLoops(loop ? QMediaPlayer::Infinite : 1);
        m_player->setSource(QUrl::fromLocalFile(filePath));
        m_player->play();
    }
    void stop() override { m_player->stop(); m_image = QImage(); }
    void togglePause() override
    {
        if (isPlaying()) m_player->pause();
        else             m_player->play();
    }
    bool isPlaying() const override
    { return m_player->playbackState() == QMediaPlayer::PlayingState; }

    qint64 position() const override { return m_player->position(); }
    qint64 duration() const override { return m_player->duration(); }
    void   setPosition(qint64 ms) override { m_player->setPosition(ms); }

    void setMuted(bool muted) override { m_audio->setMuted(muted); }
    bool isMuted() const override { return m_audio->isMuted(); }

    QImage image() const override { return m_image; }
    QSize  frameSize() const override { return m_image.size(); }

    QString report() const override
    {
        // A backend that refuses the file and one that never loaded look the
        // same from outside, so both the choice and what is next to the
        // program go in.
        const QDir plugins(QCoreApplication::applicationDirPath()
                           + QStringLiteral("/multimedia"));
        return QStringLiteral("engine: Qt Multimedia, %1 of %2 ms\n"
                              "QT_MEDIA_BACKEND=%3\nmultimedia plugins: %4")
                   .arg(position()).arg(duration())
                   .arg(qEnvironmentVariable("QT_MEDIA_BACKEND",
                                             QStringLiteral("<unset>")),
                        plugins.exists()
                            ? plugins.entryList(QDir::Files).join(QLatin1String(", "))
                            : QStringLiteral("<no multimedia directory>"));
    }

private:
    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    QVideoSink   *m_sink;
    QImage        m_image;
};

} // namespace

PlayerEngine *PlayerEngine::create(QWidget *surface, QObject *parent)
{
    Q_UNUSED(surface)
    return new QtPlayerEngine(parent);
}

#endif // !Q_OS_WIN
