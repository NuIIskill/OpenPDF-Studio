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
///
/// `tip` is the untranslated source string - it is kept around so
/// retranslateUi() can run tr() over it again after a language change.
struct ToolDef
{
    QString id;
    QString icon;
    QString tip;
};

class LeftSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit LeftSidebar(AppSettings *settings, QWidget *parent = nullptr);

    /// Every tool the sidebar can show, in the order it ships with.
    static const QVector<ToolDef> &toolCatalog();

    void setActiveTool(const QString &tool);
    void setEditMode(bool on);
    void refreshTheme();
    void retranslateUi();

public Q_SLOTS:
    /// Open (or close again) the "Customize Tools" card at the + button.
    void openCustomizePopup();

Q_SIGNALS:
    void toolSelected(const QString &tool);
    void settingsRequested();

private:
    void buildLayout();
    void applyToolLayout();

    /// The saved order, filtered to ids that still exist and topped up with
    /// tools added since it was written.
    [[nodiscard]] QStringList effectiveOrder() const;

    AppSettings                 *m_settings   { nullptr };
    QVBoxLayout                 *m_toolLayout { nullptr };
    QHash<QString, IconButton *> m_toolButtons;
    QStringList                  m_visibleIds;
    IconButton                  *m_customizeBtn { nullptr };
    IconButton                  *m_settingsBtn  { nullptr };
    QPointer<ToolCustomizePopup> m_popup;
};
