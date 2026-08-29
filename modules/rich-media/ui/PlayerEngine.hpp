// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QImage>
#include <QObject>
#include <QRect>
#include <QString>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

/// What plays a file behind MediaPlayerFrame.
class PlayerEngine : public QObject
{
    Q_OBJECT

public:

    static PlayerEngine *create(QWidget *surface, QObject *parent);

    ~PlayerEngine() override = default;

    virtual void play(const QString &filePath, bool loop, bool muted) = 0;
    virtual void stop() = 0;
    virtual void togglePause() = 0;
    virtual bool isPlaying() const = 0;

    virtual qint64 position() const = 0;
    virtual qint64 duration() const = 0;
    virtual void   setPosition(qint64 milliseconds) = 0;

    virtual void setMuted(bool muted) = 0;
    virtual bool isMuted() const = 0;

    virtual bool drawsItself() const { return false; }

    virtual void setVideoRect(const QRect &rect) { Q_UNUSED(rect) }

    virtual QImage image() const { return {}; }

    virtual QSize frameSize() const = 0;

    virtual QString report() const { return QStringLiteral("engine: unknown"); }

Q_SIGNALS:
    void positionChanged(qint64 milliseconds);
    void durationChanged(qint64 milliseconds);
    void playingChanged();

    void imageChanged();
    void failed(const QString &message);

protected:
    using QObject::QObject;
};
