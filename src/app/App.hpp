#pragma once

#include <QObject>
#include <memory>

class MainWindow;
class AppSettings;

/// Application controller.
///
/// Owns the MainWindow and AppSettings.  Serves as the wiring point
/// between the QApplication event loop and the rest of the UI.
///
/// Usage (from main.cpp):
/// @code
///     App app;
///     app.startup();
/// @endcode
class App : public QObject
{
    Q_OBJECT

public:
    explicit App(QObject *parent = nullptr);
    ~App() override;

    /// Create the main window, restore settings, and show the UI.
    void startup();

    /// Persist window state and tear down gracefully.
    void shutdown();

    [[nodiscard]] MainWindow  *mainWindow()  const;
    [[nodiscard]] AppSettings *settings()    const;

private:
    void loadSettings();
    void saveSettings();

    std::unique_ptr<MainWindow>  m_mainWindow;
    std::unique_ptr<AppSettings> m_settings;
};
