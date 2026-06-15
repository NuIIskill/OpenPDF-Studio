#pragma once

#include <QFrame>

QT_BEGIN_NAMESPACE
class QTextEdit;
QT_END_NAMESPACE

class TextAnnotation : public QFrame
{
    Q_OBJECT

public:
    explicit TextAnnotation(QWidget *parent = nullptr);

Q_SIGNALS:
    void deleteRequested();
    void moved(const QPoint &from, const QPoint &to);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    static constexpr int BAR_H = 22;

    QTextEdit *m_edit         { nullptr };
    bool       m_dragging     { false };
    QPoint     m_dragOff;
    QPoint     m_posBeforeDrag;
};
