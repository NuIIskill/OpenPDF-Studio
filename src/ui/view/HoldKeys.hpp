#pragma once

#include <QCursor>
#include <QObject>
#include <QPoint>

QT_BEGIN_NAMESPACE
class QAbstractScrollArea;
class QMouseEvent;
QT_END_NAMESPACE

/// Pans the view with the hand tool or while Space is held, and reports Shift to the tools.
class HoldKeys : public QObject
{
    Q_OBJECT

public:
    explicit HoldKeys(QAbstractScrollArea *area);

    void setPanTool(bool on) { m_panTool = on; }
    bool handleMouse(QMouseEvent *e, const QPoint &viewportPos);

Q_SIGNALS:
    void shiftChanged(bool held);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    void setShift(bool held);
    void endSpace();

    QAbstractScrollArea *m_area { nullptr };

    bool    m_panTool { false };
    bool    m_shift   { false };
    bool    m_space   { false };
    bool    m_panning { false };
    QPoint  m_panStart;
    QPoint  m_scrollOrigin;
    QCursor m_cursor;
};
