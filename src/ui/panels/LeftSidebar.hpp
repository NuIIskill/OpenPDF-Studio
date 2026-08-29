#pragma once

#include <QWidget>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
QT_END_NAMESPACE

class AppSettings;
class IconButton;
class ToolCustomizePopup;

/// One entry of the sidebar's tool catalog.
struct ToolDef
{
    QString id;
    QString icon;
    QString tip;

    bool    needsEditMode { false };
};

class LeftSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit LeftSidebar(AppSettings *settings, QWidget *parent = nullptr);

    static const QVector<ToolDef> &toolCatalog();

    static void registerTool(const ToolDef &tool);

    void setActiveTool(const QString &tool);
    void setEditMode(bool on);
    void refreshTheme();
    void retranslateUi();

public Q_SLOTS:

    void openCustomizePopup();

Q_SIGNALS:
    void toolSelected(const QString &tool);
    void settingsRequested();

private:
    void buildLayout();
    void applyToolLayout();

    [[nodiscard]] QStringList effectiveOrder() const;

    AppSettings                 *m_settings   { nullptr };
    QVBoxLayout                 *m_toolLayout { nullptr };
    QHash<QString, IconButton *> m_toolButtons;
    QStringList                  m_visibleIds;
    IconButton                  *m_customizeBtn { nullptr };
    IconButton                  *m_settingsBtn  { nullptr };
    QPointer<ToolCustomizePopup> m_popup;
};
