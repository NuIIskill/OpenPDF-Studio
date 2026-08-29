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
class MediaPlayerFrame : public QWidget
{
    Q_OBJECT

public:
    explicit MediaPlayerFrame(QWidget *parent = nullptr);
    ~MediaPlayerFrame() override;

    void setPoster(const QImage &poster);

    void play(const QString &filePath, bool showControls = true,
              bool loop = false, bool muted = false);
    void stop();

    QString engineReport() const;

Q_SIGNALS:
    void closeRequested();

    void failed(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void buildUi();
    void layOutBar();
    void updatePlayIcon();

    void noteProgress();

    void rescale();

    QRectF videoRect() const;
    static QString formatTime(qint64 milliseconds);

    PlayerEngine *m_engine { nullptr };

    QImage        m_source;
    QPixmap       m_scaled;

    bool          m_scaledStale { false };
    QImage        m_poster;
    bool          m_started { false };

    QWidget      *m_bar       { nullptr };
    QPushButton  *m_playPause { nullptr };
    QPushButton  *m_mute      { nullptr };
    QPushButton  *m_close     { nullptr };
    QSlider      *m_position  { nullptr };
    QLabel       *m_time      { nullptr };

    QTimer       *m_watchdog { nullptr };
    bool          m_seeking  { false };
};
