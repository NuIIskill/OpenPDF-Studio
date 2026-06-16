#pragma once

#include <QWidget>
#include <QList>

class IconButton;

class LeftSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit LeftSidebar(QWidget *parent = nullptr);

    void setActiveTool(const QString &tool);
    void setEditMode(bool on);
    void refreshTheme();
    void retranslateUi();

Q_SIGNALS:
    void toolSelected(const QString &tool);
    void settingsRequested();

private:
    void buildLayout();

    QList<IconButton *> m_toolButtons;
    QList<QString>      m_toolTips;
    IconButton         *m_settingsBtn { nullptr };
};
