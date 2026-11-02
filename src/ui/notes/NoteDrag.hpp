#pragma once

#include <QObject>
#include <QPixmap>
#include <QPoint>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QLabel;
class QWidget;
QT_END_NAMESPACE

/// Lets a note marker be dragged out of a widget and dropped at a point in the window.
class NoteDrag : public QObject
{
    Q_OBJECT

public:
    static constexpr int MarkerSize = 26;

    enum class Grab { Center, PressPoint };

    NoteDrag(QWidget *source, Grab grab = Grab::Center);

    void setEnabled(bool enabled) { m_enabled = enabled; }
    void setGhost(const QPixmap &pixmap) { m_ghostPixmap = pixmap; }

Q_SIGNALS:
    void started();
    void moved(const QPoint &globalTopLeft);
    void dropped(const QPoint &globalTopLeft);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget          *m_source { nullptr };
    Grab              m_grab { Grab::Center };
    bool              m_enabled { true };
    bool              m_armed { false };
    QPoint            m_pressGlobal;
    QPoint            m_hotspot;
    QPointer<QLabel>  m_ghost;
    QPixmap           m_ghostPixmap;
};
