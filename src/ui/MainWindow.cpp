#include "MainWindow.hpp"

#include "TopToolbar.hpp"
#include "LeftSidebar.hpp"
#include "DocumentView.hpp"
#include "RightSidebar.hpp"
#include "StatusBar.hpp"
#include "SettingsPanel.hpp"
#include "ui/theme/Theme.hpp"
#include "app/AppSettings.hpp"

#include <QApplication>
#include <QSplitter>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(AppSettings *settings, QWidget *parent)
    : QMainWindow(parent)
    , m_appSettings(settings)
{
    setWindowTitle(QStringLiteral("OpenPDF Studio"));
    setMinimumSize(1280, 800);
    buildUi();
    connectSignals();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_topToolbar = new TopToolbar(central);
    root->addWidget(m_topToolbar);

    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->setHandleWidth(1);
    m_splitter->setChildrenCollapsible(false);

    m_leftSidebar  = new LeftSidebar(m_splitter);
    m_documentView = new DocumentView(m_splitter);
    m_rightSidebar = new RightSidebar(m_splitter);

    m_splitter->addWidget(m_leftSidebar);
    m_splitter->addWidget(m_documentView);
    m_splitter->addWidget(m_rightSidebar);

    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);

    root->addWidget(m_splitter, 1);

    m_statusBar = new StatusBar(central);
    root->addWidget(m_statusBar);
}

void MainWindow::connectSignals()
{
    connect(m_leftSidebar, &LeftSidebar::toolSelected,    this, &MainWindow::onToolSelected);
    connect(m_topToolbar,  &TopToolbar::zoomInRequested,  this, &MainWindow::onZoomIn);
    connect(m_topToolbar,  &TopToolbar::zoomOutRequested, this, &MainWindow::onZoomOut);
    connect(m_leftSidebar, &LeftSidebar::settingsRequested, this, [this]() {
        auto *panel = new SettingsPanel(m_appSettings, this);
        connect(panel, &SettingsPanel::themeChangeRequested, this, &MainWindow::applyTheme);
        panel->showNear(m_leftSidebar);
    });
}

void MainWindow::applyTheme(const QString &mode)
{
    Theme::apply(mode);
    m_topToolbar->refreshTheme();
    m_leftSidebar->refreshTheme();
    m_rightSidebar->refreshTheme();
    m_statusBar->refreshTheme();
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void MainWindow::onZoomIn()
{
    m_zoom = qMin(m_zoom + 10, 300);
    m_documentView->setZoom(m_zoom);
    m_topToolbar->setZoom(m_zoom);
}

void MainWindow::onZoomOut()
{
    m_zoom = qMax(m_zoom - 10, 25);
    m_documentView->setZoom(m_zoom);
    m_topToolbar->setZoom(m_zoom);
}

void MainWindow::onToolSelected(const QString & /*tool*/) {}
