#include "MainWindow.hpp"

#include "TopToolbar.hpp"
#include "LeftSidebar.hpp"
#include "DocumentView.hpp"
#include "RightSidebar.hpp"
#include "StatusBar.hpp"
#include "SettingsPanel.hpp"
#include "ui/organizer/PdfOrganizerDialog.hpp"
#include "ui/theme/Theme.hpp"
#include "app/AppSettings.hpp"

#include <QApplication>
#include <QFileDialog>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

#ifdef HAVE_QT_PRINT
#include <QPrintDialog>
#include <QPrinter>
#endif

MainWindow::MainWindow(AppSettings *settings, QWidget *parent)
    : QMainWindow(parent)
    , m_appSettings(settings)
{
    setWindowTitle(QStringLiteral("OpenPDF Studio"));
    setMinimumSize(1280, 800);
    buildUi();
    connectSignals();

    // Apply persisted language on startup
    const QString lang = settings->language();
    if (lang != QLatin1String("en"))
        applyLanguage(lang);
}

// ── UI construction ───────────────────────────────────────────────────────────

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

    m_docStack = new QStackedWidget(m_splitter);

    m_rightSidebar = new RightSidebar(m_splitter);

    m_splitter->addWidget(m_leftSidebar);
    m_splitter->addWidget(m_docStack);
    m_splitter->addWidget(m_rightSidebar);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);

    root->addWidget(m_splitter, 1);

    m_statusBar = new StatusBar(central);
    root->addWidget(m_statusBar);

    // Create first tab
    addDocView();
}

// ── Signal wiring ─────────────────────────────────────────────────────────────

void MainWindow::connectSignals()
{
    // Toolbar
    connect(m_topToolbar, &TopToolbar::newTabRequested,       this, &MainWindow::onNewTab);
    connect(m_topToolbar, &TopToolbar::tabActivated,          this, &MainWindow::onTabActivated);
    connect(m_topToolbar, &TopToolbar::tabCloseRequested,     this, &MainWindow::onTabCloseRequested);
    connect(m_topToolbar, &TopToolbar::openFileRequested,     this, &MainWindow::onOpenFile);
    connect(m_topToolbar, &TopToolbar::saveRequested,         this, &MainWindow::onSave);
    connect(m_topToolbar, &TopToolbar::printRequested,        this, &MainWindow::onPrint);
    connect(m_topToolbar, &TopToolbar::undoRequested,         this, &MainWindow::onUndo);
    connect(m_topToolbar, &TopToolbar::redoRequested,         this, &MainWindow::onRedo);
    connect(m_topToolbar, &TopToolbar::zoomInRequested,       this, &MainWindow::onZoomIn);
    connect(m_topToolbar, &TopToolbar::zoomOutRequested,      this, &MainWindow::onZoomOut);

    // Left sidebar
    connect(m_leftSidebar, &LeftSidebar::toolSelected,        this, &MainWindow::onToolSelected);
    connect(m_leftSidebar, &LeftSidebar::settingsRequested,   this, [this]() {
        auto *panel = new SettingsPanel(m_appSettings, this);
        connect(panel, &SettingsPanel::themeChangeRequested,    this, &MainWindow::applyTheme);
        connect(panel, &SettingsPanel::languageChangeRequested, this, &MainWindow::applyLanguage);
        panel->open();
    });

    // Right sidebar
    connect(m_rightSidebar, &RightSidebar::modeSelected, this, &MainWindow::onModeSelected);

    // Status bar
    connect(m_statusBar, &StatusBar::previousPageRequested, this, [this]() {
        // Page navigation could be extended later
    });
    connect(m_statusBar, &StatusBar::nextPageRequested, this, [this]() {
    });
}

// ── Tab management ────────────────────────────────────────────────────────────

DocumentView *MainWindow::addDocView()
{
    auto *dv = new DocumentView(m_docStack);
    m_docViews.append(dv);
    m_docStack->addWidget(dv);

    const int idx = m_topToolbar->addTab();

    connect(dv, &DocumentView::fileOpened, this, [this, dv](const QString &path, int pages) {
        const int i = m_docViews.indexOf(dv);
        if (i >= 0) {
            const QFileInfo fi(path);
            m_topToolbar->setTabLabel(i, fi.fileName());
        }
        m_statusBar->setPageInfo(1, pages);
    });

    m_docStack->setCurrentWidget(dv);
    m_topToolbar->setCurrentTab(idx);
    return dv;
}

DocumentView *MainWindow::currentDocView() const
{
    return qobject_cast<DocumentView *>(m_docStack->currentWidget());
}

void MainWindow::onNewTab()
{
    addDocView();
}

void MainWindow::onTabActivated(int index)
{
    if (index < 0 || index >= m_docViews.size()) return;
    m_docStack->setCurrentWidget(m_docViews[index]);
    m_topToolbar->setCurrentTab(index);

    const DocumentView *dv = m_docViews[index];
    m_statusBar->setPageInfo(1, dv->pageCount() > 0 ? dv->pageCount() : 1);
    m_topToolbar->setZoom(m_zoom);
}

void MainWindow::onTabCloseRequested(int index)
{
    if (m_docViews.size() <= 1) {
        // Last tab: reset to empty state instead of closing
        m_docViews[0]->clearDocument();
        m_topToolbar->setTabLabel(0, {});
        m_statusBar->setPageInfo(1, 1);
        return;
    }

    DocumentView *dv = m_docViews.takeAt(index);
    m_docStack->removeWidget(dv);
    dv->deleteLater();
    m_topToolbar->removeTab(index);

    if (!m_docViews.isEmpty()) {
        const int next = qMin(index, m_docViews.size() - 1);
        m_docStack->setCurrentWidget(m_docViews[next]);
        m_topToolbar->setCurrentTab(next);
    }
}

// ── File operations ───────────────────────────────────────────────────────────

void MainWindow::onOpenFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open PDF"), {}, tr("PDF files (*.pdf)"));
    if (path.isEmpty()) return;

    DocumentView *dv = currentDocView();
    if (!dv) return;

    dv->openFile(path);
    m_appSettings->setLastOpenedFile(path);
    m_appSettings->sync();
}

void MainWindow::onSave()
{
    DocumentView *dv = currentDocView();
    if (!dv || dv->currentFile().isEmpty()) return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save PDF As"), dv->currentFile(), tr("PDF files (*.pdf)"));
    Q_UNUSED(path)
    // Full PDF save (flatten annotations) is a future enhancement
}

void MainWindow::onPrint()
{
#ifdef HAVE_QT_PRINT
    DocumentView *dv = currentDocView();
    if (!dv) return;
    QPrinter printer;
    QPrintDialog dlg(&printer, this);
    dlg.exec();
#endif
}

void MainWindow::onUndo()
{
    if (DocumentView *dv = currentDocView())
        dv->undoStack()->undo();
}

void MainWindow::onRedo()
{
    if (DocumentView *dv = currentDocView())
        dv->undoStack()->redo();
}

void MainWindow::onZoomIn()
{
    m_zoom = qMin(m_zoom + 10, 300);
    m_topToolbar->setZoom(m_zoom);
    if (DocumentView *dv = currentDocView())
        dv->setZoom(m_zoom);
}

void MainWindow::onZoomOut()
{
    m_zoom = qMax(m_zoom - 10, 25);
    m_topToolbar->setZoom(m_zoom);
    if (DocumentView *dv = currentDocView())
        dv->setZoom(m_zoom);
}

// ── Mode / Tool selection ─────────────────────────────────────────────────────

void MainWindow::onModeSelected(const QString &mode)
{
    if (mode == QLatin1String("organize")) {
        DocumentView *dv = currentDocView();
        const QString file = dv ? dv->currentFile() : QString{};
        auto *dlg = new PdfOrganizerDialog(file, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->open();
    }
}

void MainWindow::onToolSelected(const QString &tool)
{
    m_activeTool = tool;
    DocumentView *dv = currentDocView();
    if (!dv) return;

    if (tool == QLatin1String("select"))
        dv->setTool(DocumentView::Tool::Select);
    else if (tool == QLatin1String("pan"))
        dv->setTool(DocumentView::Tool::Pan);
    else if (tool == QLatin1String("text"))
        dv->setTool(DocumentView::Tool::Text);
    else if (tool == QLatin1String("comment"))
        dv->setTool(DocumentView::Tool::Comment);
    else if (tool == QLatin1String("image"))
        dv->setTool(DocumentView::Tool::Image);
}

// ── Theme / Language ──────────────────────────────────────────────────────────

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

void MainWindow::applyLanguage(const QString &lang)
{
    QApplication::removeTranslator(&m_translator);

    if (lang != QLatin1String("en")) {
        const QString path = QStringLiteral(":/i18n/openpdf_%1.qm").arg(lang);
        if (m_translator.load(path))
            QApplication::installTranslator(&m_translator);
    }

    m_appSettings->setLanguage(lang);
    m_appSettings->sync();

    retranslateUi();
}

void MainWindow::retranslateUi()
{
    m_topToolbar->retranslateUi();
    m_leftSidebar->retranslateUi();
    m_rightSidebar->retranslateUi();
    m_statusBar->retranslateUi();
    for (DocumentView *dv : m_docViews)
        dv->retranslateUi();
}

void MainWindow::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QMainWindow::changeEvent(e);
}
