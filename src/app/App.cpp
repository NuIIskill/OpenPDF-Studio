#include "App.hpp"

#include "AppSettings.hpp"
#include "ui/MainWindow.hpp"
#include "ui/DocumentView.hpp"
#include "ui/TopToolbar.hpp"
#include "ui/StatusBar.hpp"

#include <QApplication>
#include <QDebug>

App::App(QObject *parent)
    : QObject(parent)
    , m_settings(std::make_unique<AppSettings>())
{}

App::~App()
{
    saveSettings();
}

void App::startup()
{
    m_mainWindow = std::make_unique<MainWindow>(m_settings.get());
    loadSettings();
    m_mainWindow->show();
    // Re-apply theme after widgets are visible so refreshTheme() + unpolish/polish take effect
    m_mainWindow->applyTheme(m_settings->theme());
}

void App::shutdown()
{
    saveSettings();
}

MainWindow *App::mainWindow() const
{
    return m_mainWindow.get();
}

AppSettings *App::settings() const
{
    return m_settings.get();
}

// ── Private ────────────────────────────────────────────────────────────────

void App::loadSettings()
{
    if (!m_mainWindow)
        return;

    const QByteArray geo = m_settings->windowGeometry();
    if (!geo.isEmpty())
        m_mainWindow->restoreGeometry(geo);

    const QByteArray state = m_settings->windowState();
    if (!state.isEmpty())
        m_mainWindow->restoreState(state);

    qDebug() << "App: settings loaded";
}

void App::saveSettings()
{
    if (!m_mainWindow)
        return;

    m_settings->setWindowGeometry(m_mainWindow->saveGeometry());
    m_settings->setWindowState(m_mainWindow->saveState());
    m_settings->sync();

    qDebug() << "App: settings saved";
}
