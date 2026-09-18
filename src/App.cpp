#include "App.hpp"

#include "app/AppSettings.hpp"
#include "app/SessionStore.hpp"
#include "ui/MainWindow.hpp"
#include "ui/DocumentView.hpp"
#include "ui/bars/TopToolbar.hpp"
#include "ui/bars/StatusBar.hpp"

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

    SessionStore::pruneSnapshots();

    m_mainWindow = std::make_unique<MainWindow>(m_settings.get());
    loadSettings();
    m_mainWindow->show();

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
