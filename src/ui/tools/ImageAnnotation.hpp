#pragma once

#include <QFrame>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

class ImageAnnotation : public QFrame
{
    Q_OBJECT

public:
    explicit ImageAnnotation(const QString &imagePath, QWidget *parent = nullptr);

Q_SIGNALS:
    void deleteRequested();
    void moved(const QPoint &from, const QPoint &to);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    static constexpr int BAR_H = 22;
    static constexpr int MAX_W = 300;
    static constexpr int MAX_H = 260;

    QLabel *m_imgLabel    { nullptr };
    bool    m_dragging    { false };
    QPoint  m_dragOff;
    QPoint  m_posBeforeDrag;
};
