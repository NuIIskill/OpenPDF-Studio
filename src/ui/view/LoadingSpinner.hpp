#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

/// Spinning indicator centred over the document while pages are loading or a save runs.
class LoadingSpinner : public QWidget
{
    Q_OBJECT

public:
    explicit LoadingSpinner(QWidget *parent);

    void setBusy(bool busy);
    void showNow();

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;

private:
    void centre();

    QTimer *m_delay { nullptr };
    QTimer *m_frame { nullptr };
    int     m_angle { 0 };
    bool    m_busy  { false };
};
