#include "MainWindow.hpp"

#include "TopToolbar.hpp"
#include "LeftSidebar.hpp"
#include "DocumentView.hpp"

#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
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

    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_topToolbar = new TopToolbar(central);
    rootLayout->addWidget(m_topToolbar);

    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->setHandleWidth(1);
    m_splitter->setChildrenCollapsible(false);

    m_leftSidebar  = new LeftSidebar(m_splitter);
    m_documentView = new DocumentView(m_splitter);

    m_splitter->addWidget(m_leftSidebar);
    m_splitter->addWidget(m_documentView);

    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    rootLayout->addWidget(m_splitter, 1);
}

void MainWindow::connectSignals()
{
    connect(m_leftSidebar, &LeftSidebar::toolSelected,   this, &MainWindow::onToolSelected);
    connect(m_topToolbar,  &TopToolbar::zoomInRequested,  this, &MainWindow::onZoomIn);
    connect(m_topToolbar,  &TopToolbar::zoomOutRequested, this, &MainWindow::onZoomOut);
}

void MainWindow::onZoomIn()
{
    m_zoom = qMin(m_zoom + 10, 300);
    m_topToolbar->setZoom(m_zoom);
}

void MainWindow::onZoomOut()
{
    m_zoom = qMax(m_zoom - 10, 25);
    m_topToolbar->setZoom(m_zoom);
}

void MainWindow::onToolSelected(const QString & /*tool*/)
{
    // Future: switch active editing tool
}
