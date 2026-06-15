#pragma once

#include <QFrame>

QT_BEGIN_NAMESPACE
class QTextEdit;
class QPushButton;
QT_END_NAMESPACE

class CommentAnnotation : public QFrame
{
    Q_OBJECT

public:
    explicit CommentAnnotation(QWidget *parent = nullptr);

Q_SIGNALS:
    void deleteRequested();
    void moved(const QPoint &from, const QPoint &to);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    void toggle();

    static constexpr int BAR_H       = 22;
    static constexpr int EXPANDED_H  = 130;
    static constexpr int COLLAPSED_H = 22;
    static constexpr int WIDTH       = 220;

    QTextEdit  *m_edit      { nullptr };
    QPushButton *m_toggleBtn { nullptr };
    bool        m_expanded  { true };
    bool        m_dragging  { false };
    QPoint      m_dragOff;
    QPoint      m_posBeforeDrag;
};
