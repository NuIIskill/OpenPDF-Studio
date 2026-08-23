#pragma once

#include <QFrame>
#include <QStringList>

class QListWidget;

/// The "Customize Tools" card that drops out of the LeftSidebar's + button.
///
/// Lists every tool in the sidebar's catalog in the user's own order, each
/// with a checkbox; rows are dragged to reorder. There is no OK button - every
/// change is reported through configChanged() straight away, exactly as the
/// sidebar shows it.
///
/// It is a plain child widget of the window, not a Qt::Popup: a popup window
/// holds the mouse grab, and drag-to-reorder inside one does not work.
/// Clicking anywhere outside closes it instead.
class ToolCustomizePopup : public QFrame
{
    Q_OBJECT

public:
    /// `order` is the full list of tool ids as the sidebar has them,
    /// `hidden` the subset with its checkbox off.
    ToolCustomizePopup(const QStringList &order,
                       const QStringList &hidden,
                       QWidget *parent = nullptr);

    /// Place the card beside `anchor` and show it, kept inside the window.
    void popupAt(QWidget *anchor);

Q_SIGNALS:
    /// The full order plus the ids that are switched off.
    void configChanged(const QStringList &order, const QStringList &hidden);
    /// "Reset to default" - back to the built-in order with nothing hidden.
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
