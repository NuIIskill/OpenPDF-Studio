#pragma once

#include <QWidget>
#include <QList>

class IconButton;

/// Narrow vertical tool strip — light background, left edge of window.
///
/// Fixed width: 60 px. Tools at top, settings pinned to bottom.
/// Selected tool shown with rounded blue-tinted background.
class LeftSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit LeftSidebar(QWidget *parent = nullptr);

    void setActiveTool(const QString &tool);
    void refreshTheme();

Q_SIGNALS:
    void toolSelected(const QString &tool);
    void settingsRequested();

private:
    void buildLayout();

    QList<IconButton *> m_toolButtons;
    IconButton *m_settingsBtn { nullptr };
};
