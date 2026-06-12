#pragma once

#include <QMainWindow>

class TopToolbar;
class LeftSidebar;
class DocumentView;

QT_BEGIN_NAMESPACE
class QSplitter;
QT_END_NAMESPACE

/// The application's main window.
///
/// Layout (top→bottom):
///   TopToolbar  (56 px) — app name, file name, save, print, zoom
///   ─────────────────────────────────────────────────────────────
///   QSplitter  (LeftSidebar | DocumentView)
///     LeftSidebar: tool buttons top, settings bottom
///
/// Minimum size: 1280×800.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

    [[nodiscard]] TopToolbar   *topToolbar()   const { return m_topToolbar;   }
    [[nodiscard]] LeftSidebar  *leftSidebar()  const { return m_leftSidebar;  }
    [[nodiscard]] DocumentView *documentView() const { return m_documentView; }

private:
    void buildUi();
    void connectSignals();
    void onToolSelected(const QString &tool);
    void onZoomIn();
    void onZoomOut();

    TopToolbar   *m_topToolbar   { nullptr };
    LeftSidebar  *m_leftSidebar  { nullptr };
    DocumentView *m_documentView { nullptr };
    QSplitter    *m_splitter     { nullptr };

    int m_zoom { 100 };
};
