#pragma once

#include <QMainWindow>

class TopToolbar;
class LeftSidebar;
class DocumentView;
class RightSidebar;
class StatusBar;

QT_BEGIN_NAMESPACE
class QSplitter;
QT_END_NAMESPACE

/// Application main window.
///
///  TopToolbar (56 px)
///  ──────────────────────────────────────────────────
///  QSplitter: LeftSidebar(60) | DocumentView | RightSidebar(110)
///  ──────────────────────────────────────────────────
///  StatusBar (48 px) — page navigation
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

    [[nodiscard]] TopToolbar   *topToolbar()   const { return m_topToolbar;   }
    [[nodiscard]] LeftSidebar  *leftSidebar()  const { return m_leftSidebar;  }
    [[nodiscard]] DocumentView *documentView() const { return m_documentView; }
    [[nodiscard]] RightSidebar *rightSidebar() const { return m_rightSidebar; }
    [[nodiscard]] StatusBar    *statusBar()    const { return m_statusBar;    }

private:
    void buildUi();
    void connectSignals();
    void onToolSelected(const QString &tool);
    void onZoomIn();
    void onZoomOut();

    TopToolbar   *m_topToolbar   { nullptr };
    LeftSidebar  *m_leftSidebar  { nullptr };
    DocumentView *m_documentView { nullptr };
    RightSidebar *m_rightSidebar { nullptr };
    StatusBar    *m_statusBar    { nullptr };
    QSplitter    *m_splitter     { nullptr };

    int m_zoom { 100 };
};
