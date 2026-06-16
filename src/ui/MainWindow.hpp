#pragma once

#include <QMainWindow>
#include <QTranslator>

class TopToolbar;
class LeftSidebar;
class DocumentView;
class RightSidebar;
class StatusBar;
class AppSettings;
class SettingsPanel;

QT_BEGIN_NAMESPACE
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

public Q_SLOTS:
    void applyTheme(const QString &mode);
    void applyLanguage(const QString &lang);

protected:
    void changeEvent(QEvent *e) override;

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
    void onPrint();
    void onUndo();
    void onRedo();
    void onZoomIn();
    void onZoomOut();
    void onModeSelected(const QString &mode);
    void onToolSelected(const QString &tool);

    AppSettings  *m_appSettings  { nullptr };
    TopToolbar   *m_topToolbar   { nullptr };
    LeftSidebar  *m_leftSidebar  { nullptr };
    RightSidebar *m_rightSidebar { nullptr };
    StatusBar    *m_statusBar    { nullptr };
    QSplitter    *m_splitter     { nullptr };

    QStackedWidget        *m_docStack  { nullptr };
    QList<DocumentView *>  m_docViews;

    QTranslator m_translator;
    int         m_zoom         { 100 };
    QString     m_activeTool   { QStringLiteral("select") };
    bool        m_editMode     { false };
};
