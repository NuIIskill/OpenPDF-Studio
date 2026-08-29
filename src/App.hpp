#pragma once

#include <QObject>
#include <memory>

class MainWindow;
class AppSettings;

/// Application controller.
class App : public QObject
{
    Q_OBJECT

public:
    explicit App(QObject *parent = nullptr);
    ~App() override;

    void startup();

    void shutdown();

    [[nodiscard]] MainWindow  *mainWindow()  const;
    [[nodiscard]] AppSettings *settings()    const;

private:
    void loadSettings();
    void saveSettings();

    std::unique_ptr<MainWindow>  m_mainWindow;
    std::unique_ptr<AppSettings> m_settings;
};
