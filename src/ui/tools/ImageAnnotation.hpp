#pragma once

#include <QFrame>
#include <QPixmap>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
QT_END_NAMESPACE

class ImageAnnotation : public QFrame
{
    Q_OBJECT

public:
    explicit ImageAnnotation(const QString &imagePath, QWidget *parent = nullptr);

    void setEditActive(bool on);

Q_SIGNALS:
    void deleteRequested();
    void moved(const QPoint &from, const QPoint &to);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;

private:
    static constexpr int RESIZE_SZ = 10;
    static constexpr int MIN_W = 80;
    static constexpr int MIN_H = 60;
    static constexpr int MAX_W = 600;
    static constexpr int MAX_H = 500;

    bool isInResizeHandle(const QPoint &p) const;

    QLabel      *m_imgLabel  { nullptr };
    QPushButton *m_deleteBtn { nullptr };
    QPixmap      m_origPixmap;

    bool   m_editActive  { true };
    bool   m_hovered     { false };
    bool   m_dragging    { false };
    bool   m_resizing    { false };
    QPoint m_dragOff;
    QPoint m_posBeforeDrag;
    QPoint m_resizeOrigin;
    QSize  m_sizeAtResize;
};
