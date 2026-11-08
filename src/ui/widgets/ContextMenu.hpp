#pragma once

#include <QMenu>

/// Right-click menu in the shared canvas style that draws its destructive entry in the danger colour.
class ContextMenu : public QMenu
{
public:
    explicit ContextMenu(QWidget *parent = nullptr);

    using QMenu::addAction;
    QAction *addAction(const char *icon, const QString &text);
    QAction *addDangerAction(const char *icon, const QString &text);

protected:
    void initStyleOption(QStyleOptionMenuItem *option, const QAction *action) const override;

private:
    QAction *m_danger { nullptr };
};
