#pragma once

#include <QEvent>
#include <QPointer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QAbstractScrollArea;
class QScrollBar;
QT_END_NAMESPACE

/// An invisible strip beside a slim scroll bar's handle that grabs the handle too, so the bar is easier to hit without taking more room.
class ScrollBarReach : public QWidget
{
    Q_OBJECT

public:
    ScrollBarReach(QScrollBar *bar, QAbstractScrollArea *area);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    void place();
    QPointF onBar(const QPointF &global) const;
    void forward(QMouseEvent *e);
    void hover(QEvent::Type type, const QPointF &global);

    QPointer<QScrollBar> m_bar;
    QAbstractScrollArea *m_area { nullptr };
};
