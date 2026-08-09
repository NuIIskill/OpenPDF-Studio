#pragma once

#include "ui/dialogs/ExportDialog.hpp"

#include <QMainWindow>
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
    // Carries out an export the dialog has already validated.
    void runExport(DocumentView *dv, const ExportRequest &req);
    void onToolSelected(const QString &tool);
    void onStartPresentation();

    bool confirmAndSave(DocumentView *dv);
    void openTextPanel();
    void closeTextPanel();
    void loadShortcuts();
    void loadZoomSettings();

    AppSettings          *m_appSettings  { nullptr };
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
