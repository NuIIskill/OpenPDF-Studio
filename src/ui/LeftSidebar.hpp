#pragma once

#include <QWidget>
#include <QList>

class IconButton;

/// Narrow vertical toolbar on the left edge of the main window.
///
/// Fixed width: 56 px. Dark background.
/// Tool buttons at the top, settings button pinned to the bottom.
class LeftSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit LeftSidebar(QWidget *parent = nullptr);

    void setActiveTool(const QString &tool);

Q_SIGNALS:
    void toolSelected(const QString &tool);
    void settingsRequested();

private:
    void buildLayout();

    QList<IconButton *> m_toolButtons;
};
