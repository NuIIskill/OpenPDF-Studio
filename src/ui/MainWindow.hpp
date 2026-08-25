#pragma once

#include "ui/export/ExportDialog.hpp"

#include <QMainWindow>
#include <QHash>
#include <QMap>
#include <QTranslator>

class TopToolbar;
class LeftSidebar;
class DocumentView;
class RightSidebar;
class TextPropertiesPanel;
class FormatBar;
class StatusBar;
class AppSettings;
class SettingsPanel;
class UpdateChecker;

QT_BEGIN_NAMESPACE
class QShortcut;
class QStackedWidget;
class QSplitter;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(AppSettings *settings, QWidget *parent = nullptr);
    ~MainWindow() override = default;

    [[nodiscard]] TopToolbar   *topToolbar()   const { return m_topToolbar;   }
    [[nodiscard]] LeftSidebar  *leftSidebar()  const { return m_leftSidebar;  }
    [[nodiscard]] RightSidebar *rightSidebar() const { return m_rightSidebar; }
    [[nodiscard]] StatusBar    *statusBar()    const { return m_statusBar;    }

    // Open a PDF in the current tab (command line, file association).
    void openPath(const QString &path);

public Q_SLOTS:
    void applyTheme(const QString &mode);
    void applyLanguage(const QString &lang);

protected:
    void changeEvent(QEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private:
    void retranslateUi();
    void buildUi();
    void connectSignals();

    DocumentView *addDocView();
    DocumentView *currentDocView() const;

    void onNewTab();
    void onTabActivated(int index);
    void onTabCloseRequested(int index);
    void onOpenFile();
    void onSave();
    void onSaveAs();
    void onPrint();
    void onUndo();
    void onRedo();
    void onZoomIn();
    void onZoomOut();
    void onModeSelected(const QString &mode);
    // The document's change log, with the buttons that walk it. Window-modal
    // like the organizer: it acts on the document in the tab it was opened on.
    void openHistoryDialog();
    // Carries out an export the dialog has already validated.
    void runExport(DocumentView *dv, const ExportRequest &req);
    void onToolSelected(const QString &tool);
    void onStartPresentation();

    // Writes the document and reports a failure to the user. Every save goes
    // through here: a save that quietly does nothing is indistinguishable from
    // one that worked until the file is opened again somewhere else.
    bool saveDocument(DocumentView *dv, const QString &path);
    bool confirmAndSave(DocumentView *dv);
    void openTextPanel();
    void closeTextPanel();
    void loadShortcuts();
    void loadZoomSettings();

    // Settings dialog, wired up and shown. Returned so a caller can jump to a
    // particular page.
    SettingsPanel *openSettings();
    // The license notices themselves live in drm/LicenseNotice.hpp; this only
    // decides when they get their chance.
    void showLicenseNotices();

    // Asks GitHub for the newest tag, but only if the setting says so and the
    // interval has passed. Silent unless there is something newer.
    void checkForUpdates();

    // Panel layout persistence — collapsing the right strip is a deliberate
    // choice by the user, so it survives a restart unless they opt out.
    void setRightSidebarCollapsed(bool collapsed);
    void applyPanelLayout();
    void savePanelLayout();

    // Panels a tool brought with it (ToolPanels), by tool id.
    struct ToolPanelSlot { QWidget *widget { nullptr }; int width { 0 }; };
    QHash<QString, ToolPanelSlot> m_toolPanels;

    AppSettings          *m_appSettings  { nullptr };
    UpdateChecker        *m_updateChecker{ nullptr };
    TopToolbar           *m_topToolbar   { nullptr };
    FormatBar            *m_formatBar    { nullptr };
    LeftSidebar          *m_leftSidebar  { nullptr };
    TextPropertiesPanel  *m_textPanel    { nullptr };
    RightSidebar         *m_rightSidebar { nullptr };
    StatusBar            *m_statusBar    { nullptr };
    QSplitter            *m_splitter     { nullptr };

    QStackedWidget        *m_docStack  { nullptr };
    QList<DocumentView *>  m_docViews;

    QMap<QString, QShortcut *> m_shortcuts;
    QTranslator m_translator;
    int         m_zoom                   { 100 };
    QString     m_activeTool             { QStringLiteral("select") };
    bool        m_editMode               { false };
    bool        m_textPanelOpen          { false };
    bool        m_rightSidebarCollapsed  { false };
};
