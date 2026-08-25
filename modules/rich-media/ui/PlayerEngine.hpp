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
///
/// There are two, because one is not enough. Qt Multimedia is the way
/// everywhere except Windows, where the only backend packaged for MinGW builds
/// its pipeline on Media Foundation and EVR and gives up on files other
/// programs play without trouble. Windows therefore gets the older road:
/// DirectShow, drawing into a child window of its own. That is the same route
/// Acrobat takes there, it needs nothing but what the system already has, and
/// it is decided at compile time, not at run time.
///
/// The frame above does not care which one it got. It asks for a picture and
/// is told when something happened; an engine that draws by itself says so and
/// is left the rectangle to draw in.
class PlayerEngine : public QObject
{
    Q_OBJECT

public:
    /// The engine this platform uses. `surface` is the widget it plays in.
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

    /// true → the engine puts the picture on screen itself and only needs to
    /// be told where. false → the frame asks for image() and paints it.
    virtual bool drawsItself() const { return false; }
    /// Where the picture belongs, in the surface widget's coordinates.
    virtual void setVideoRect(const QRect &rect) { Q_UNUSED(rect) }
    /// The current picture for engines that hand frames over.
    virtual QImage image() const { return {}; }
    /// Native size of the picture, invalid while unknown.
    virtual QSize frameSize() const = 0;

    /// What to put in a failure report: which road was taken and how far it
    /// got. Nothing else can tell a decoder that refuses the file apart from
    /// one that never started.
    virtual QString report() const { return QStringLiteral("engine: unknown"); }

Q_SIGNALS:
    void positionChanged(qint64 milliseconds);
    void durationChanged(qint64 milliseconds);
    void playingChanged();
    /// A new picture is available (frame-handing engines only).
    void imageChanged();
    void failed(const QString &message);

protected:
    using QObject::QObject;
};
