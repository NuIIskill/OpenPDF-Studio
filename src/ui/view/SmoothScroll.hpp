#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QAbstractScrollArea;
class QScrollBar;
class QWheelEvent;
class QWindow;
QT_END_NAMESPACE

/// Glides the view to where a mouse wheel step points instead of jumping there.
class SmoothScroll : public QObject
{
    Q_OBJECT

public:
    explicit SmoothScroll(QAbstractScrollArea *area, QObject *parent = nullptr);

    bool handleWheel(QWheelEvent *e);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Axis {
        QPointer<QScrollBar> bar;
        double position { 0 };
        double target   { 0 };
        int    lastSet  { 0 };
        bool   active   { false };
    };

    void scrollBy(Axis &axis, double distance);
    void requestFrame();
    void frame();
    bool advance(Axis &axis, double seconds);

    QAbstractScrollArea *m_area { nullptr };
    QPointer<QWindow>    m_window;
    QElapsedTimer        m_clock;
    qint64               m_lastFrame { 0 };
    bool                 m_waiting { false };
    Axis                 m_vertical;
    Axis                 m_horizontal;
};
