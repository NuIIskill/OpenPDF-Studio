#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QFrame;
class QGraphicsOpacityEffect;
class QLabel;
class QPropertyAnimation;
class QTimer;
QT_END_NAMESPACE

/// Transient overlay confirming that a document was written to disk.
class SaveIndicator : public QWidget
{
    Q_OBJECT

public:
    explicit SaveIndicator(QWidget *parent);

    void flash(const QString &fileName);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reposition();
    void fadeOut();

    QFrame                 *m_pill    { nullptr };
    QLabel                 *m_icon    { nullptr };
    QLabel                 *m_text    { nullptr };
    QGraphicsOpacityEffect *m_opacity { nullptr };
    QPropertyAnimation     *m_fade    { nullptr };
    QTimer                 *m_hold    { nullptr };
};
