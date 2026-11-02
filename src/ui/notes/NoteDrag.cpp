#include "ui/notes/NoteDrag.hpp"

#include "ui/theme/Theme.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QWidget>

NoteDrag::NoteDrag(QWidget *source, Grab grab)
    : QObject(source)
    , m_source(source)
    , m_grab(grab)
{
    source->installEventFilter(this);
}

bool NoteDrag::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_source) return false;
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton || !m_enabled || !m_source->isEnabled()) break;
        m_armed       = true;
        m_pressGlobal = mouse->globalPosition().toPoint();
        const QSize ghost = m_ghostPixmap.isNull()
            ? QSize(MarkerSize, MarkerSize) : m_ghostPixmap.deviceIndependentSize().toSize();
        m_hotspot     = m_grab == Grab::PressPoint
            ? mouse->position().toPoint() : QPoint(ghost.width() / 2, ghost.height() / 2);
        break;
    }
    case QEvent::MouseMove: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (!m_armed || !(mouse->buttons() & Qt::LeftButton)) break;
        const QPoint global = mouse->globalPosition().toPoint();
        if (!m_ghost) {
            if ((global - m_pressGlobal).manhattanLength()
                    < QApplication::startDragDistance())
                break;
            QWidget *host = m_source->window();
            m_ghost = new QLabel(host);
            m_ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_ghost->setAlignment(Qt::AlignCenter);
            if (m_ghostPixmap.isNull()) {
                m_ghost->setObjectName(QStringLiteral("NoteDragGhost"));
                m_ghost->setFixedSize(MarkerSize, MarkerSize);
                m_ghost->setPixmap(Theme::renderSvg(QStringLiteral("message-square"), Qt::white,
                                                    15, host->devicePixelRatioF()));
            } else {
                m_ghost->setFixedSize(m_ghostPixmap.deviceIndependentSize().toSize());
                m_ghost->setPixmap(m_ghostPixmap);
            }
            m_ghost->show();
            if (auto *button = qobject_cast<QAbstractButton *>(m_source))
                button->setDown(false);
            Q_EMIT started();
        }
        m_ghost->move(m_ghost->parentWidget()->mapFromGlobal(global - m_hotspot));
        m_ghost->raise();
        Q_EMIT moved(global - m_hotspot);
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) break;
        m_armed = false;
        if (!m_ghost) break;
        delete m_ghost;
        if (auto *button = qobject_cast<QAbstractButton *>(m_source))
            button->setDown(false);
        Q_EMIT dropped(mouse->globalPosition().toPoint() - m_hotspot);
        return true;
    }
    default:
        break;
    }
    return false;
}
