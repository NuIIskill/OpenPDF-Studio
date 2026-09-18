#pragma once

#include <QFrame>
#include <QStringList>

class QListWidget;

/// The "Customize Tools" card that drops out of the LeftSidebar's + button.
class ToolCustomizePopup : public QFrame
{
    Q_OBJECT

public:

    ToolCustomizePopup(const QStringList &order,
                       const QStringList &hidden,
                       QWidget *parent = nullptr);

    void popupAt(QWidget *anchor);

Q_SIGNALS:

    void configChanged(const QStringList &order, const QStringList &hidden);

    void resetRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void buildUi();
    void fill(const QStringList &order, const QStringList &hidden);
    void emitConfig();

    QListWidget *m_list { nullptr };
};
