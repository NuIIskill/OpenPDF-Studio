// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QImage>
#include <QPixmap>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QSlider;
class QTimer;
QT_END_NAMESPACE

class PlayerEngine;

/// The in-app player: sits exactly on the annotation and plays there.
///
/// The frame is the transport bar, the poster and the geometry. What decodes
/// is a PlayerEngine, and which one that is depends on the platform. An engine
/// that hands over pictures is painted here; one that draws into a window of
/// its own is only told where to draw.
class MediaPlayerFrame : public QWidget
{
    Q_OBJECT

public:
    explicit MediaPlayerFrame(QWidget *parent = nullptr);
    ~MediaPlayerFrame() override;

    /// Shown until playback starts.
    void setPoster(const QImage &poster);

    void play(const QString &filePath, bool showControls = true,
              bool loop = false, bool muted = false);
    void stop();

    /// What the engine has to say for a failure report.
    QString engineReport() const;

Q_SIGNALS:
    void closeRequested();
    /// Playback failed; the caller decides what to offer.
    void failed(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void buildUi();
    void layOutBar();
    void updatePlayIcon();
    /// Stops the watchdog: something did happen.
    void noteProgress();
    /// Redoes the scaled copy after a new picture or a resize.
    void rescale();
    /// Where the picture belongs: centred, aspect ratio kept.
    QRectF videoRect() const;
    static QString formatTime(qint64 milliseconds);

    PlayerEngine *m_engine { nullptr };

    /// The last picture at its own size and the copy actually blitted. Unused
    /// when the engine draws itself.
    QImage        m_source;
    QPixmap       m_scaled;
    /// The scaled copy is behind the source. Rescaling happens while painting,
    /// so pictures arriving faster than the window repaints cost nothing.
    bool          m_scaledStale { false };
    QImage        m_poster;
    bool          m_started { false };

    QWidget      *m_bar       { nullptr };
    QPushButton  *m_playPause { nullptr };
    QPushButton  *m_mute      { nullptr };
    QPushButton  *m_close     { nullptr };
    QSlider      *m_position  { nullptr };
    QLabel       *m_time      { nullptr };

    /// Turns silence into a message. An engine without a working decoder
    /// neither plays nor complains, and to the user the program did nothing.
    QTimer       *m_watchdog { nullptr };
    bool          m_seeking  { false };
};
