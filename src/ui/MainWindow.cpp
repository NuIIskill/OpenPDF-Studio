#include "MainWindow.hpp"

#include "TopToolbar.hpp"
#include "LeftSidebar.hpp"
#include "DocumentView.hpp"
#include "RightSidebar.hpp"
#include "TextPropertiesPanel.hpp"
#include "FormatBar.hpp"
#include "StatusBar.hpp"
#include "SettingsPanel.hpp"
#include "ui/organizer/PdfOrganizerDialog.hpp"
#include "ui/ExportDialog.hpp"
#include "engine/edit/DocxExporter.hpp"
#include "ui/theme/Theme.hpp"
#include "app/AppSettings.hpp"

#include <QApplication>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>
#include <QCloseEvent>
#include <QKeySequence>
#include <QShortcut>

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

    m_formatBar = new FormatBar(central);
    m_formatBar->hide();
    root->addWidget(m_formatBar);

    // Splitter: only left sidebar + canvas.
    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->setHandleWidth(1);
    m_splitter->setChildrenCollapsible(false);
    m_leftSidebar = new LeftSidebar(m_splitter);
    m_docStack    = new QStackedWidget(m_splitter);
    m_splitter->addWidget(m_leftSidebar);
    m_splitter->addWidget(m_docStack);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    // Panel and right strip live OUTSIDE the splitter in a plain QHBoxLayout.
    // Toggling is done via setFixedWidth(0 / kWidth) — the QHBoxLayout engine
    // immediately redistributes the freed/taken space to the splitter.
    m_textPanel    = new TextPropertiesPanel(central);
    m_rightSidebar = new RightSidebar(central);
    m_textPanel->setFixedWidth(0);   // collapsed by default
    m_textPanel->hide();

    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(m_splitter,     1);
    row->addWidget(m_textPanel,    0);
    row->addWidget(m_rightSidebar, 0);
    root->addLayout(row, 1);

    m_statusBar = new StatusBar(central);
    root->addWidget(m_statusBar);

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

    // Text properties panel – X button just closes the panel
    connect(m_textPanel, &TextPropertiesPanel::closeRequested, this, [this]() {
        closeTextPanel();
        m_rightSidebar->setMode(QString{});
    });

    // FormatBar font size → active editor live update
    connect(m_formatBar, &FormatBar::fontSizeChanged, this, [this](int pt) {
        if (DocumentView *dv = currentDocView())
            dv->setEditorFontSize(pt);
    });

    // Ctrl+S shortcut
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &MainWindow::onSave);

    // Status bar
    connect(m_statusBar, &StatusBar::previousPageRequested, this, [this]() {});
    connect(m_statusBar, &StatusBar::nextPageRequested,     this, [this]() {});
    connect(m_statusBar, &StatusBar::panelToggleRequested,  this, [this]() {
        m_rightSidebarCollapsed = !m_rightSidebarCollapsed;
        if (m_rightSidebarCollapsed) {
            m_rightSidebar->hide();
            m_rightSidebar->setFixedWidth(0);
        } else {
            m_rightSidebar->setFixedWidth(RightSidebar::kWidth);
            m_rightSidebar->show();
        }
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

    // Sync FormatBar font size ↔ active editor
    connect(dv, &DocumentView::editorFontSizeChanged,
            m_formatBar, &FormatBar::setFontSize);

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
    if (index < 0 || index >= m_docViews.size()) return;
    DocumentView *dv = m_docViews[index];

    if (!confirmAndSave(dv)) return;

    if (m_docViews.size() <= 1) {
        dv->clearDocument();
        m_topToolbar->setTabLabel(0, {});
        m_statusBar->setPageInfo(1, 1);
        return;
    }

    m_docViews.takeAt(index);
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
    if (!dv) return;

    if (!dv->currentFile().isEmpty()) {
        dv->saveToFile(dv->currentFile());
    } else {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Save PDF As"), {}, tr("PDF files (*.pdf)"));
        if (!path.isEmpty())
            dv->saveToFile(path);
    }
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
    if (mode == QLatin1String("edit")) {
        m_editMode = !m_editMode;
        for (DocumentView *dv : m_docViews)
            dv->setEditMode(m_editMode);
        m_rightSidebar->setMode(m_editMode ? QStringLiteral("edit") : QString{});
        if (!m_editMode) {
            closeTextPanel();
            m_formatBar->hide();
        }
    } else if (mode == QLatin1String("export")) {
        DocumentView *dv   = currentDocView();
        const QString file = dv ? dv->currentFile() : QString{};
        const int pages    = dv ? dv->pageCount() : 1;
        ExportDialog dlg(file, pages, this);
        if (dlg.exec() == QDialog::Accepted && dv) {
            const QString path   = dlg.selectedPath();
            const QString format = dlg.selectedFormat();
            if (!path.isEmpty()) {
                if (format == QLatin1String("word")) {
                    const QList<QString> texts = dv->allPageTexts();
                    const QString title = QFileInfo(dv->currentFile()).completeBaseName();
                    if (!DocxExporter::exportToDocx(path, texts, title))
                        QMessageBox::warning(this, tr("Export failed"),
                            tr("Could not write to \"%1\".").arg(path));
                } else {
                    dv->saveToFile(path);
                }
            }
        }
    } else if (mode == QLatin1String("organize")) {
        DocumentView *dv = currentDocView();
        const QString file = dv ? dv->currentFile() : QString{};
        auto *dlg = new PdfOrganizerDialog(file, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->open();
    }
}

void MainWindow::onToolSelected(const QString &tool)
{
    static const QStringList kEditTools = {
        QStringLiteral("text"), QStringLiteral("comment"),
        QStringLiteral("draw"), QStringLiteral("image"), QStringLiteral("table"),
    };

    if (kEditTools.contains(tool) && !m_editMode) {
        const auto ans = QMessageBox::question(
            this,
            tr("Edit Mode"),
            tr("Do you want to enable Edit Mode?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);

        if (ans == QMessageBox::No) {
            m_leftSidebar->setActiveTool(QStringLiteral("select"));
            if (DocumentView *dv = currentDocView())
                dv->setTool(DocumentView::Tool::Select);
            return;
        }

        m_editMode = true;
        m_rightSidebar->setMode(QStringLiteral("edit"));
        for (DocumentView *dv : m_docViews)
            dv->setEditMode(true);
    }

    m_activeTool = tool;

    const bool isText = (tool == QLatin1String("text"));
    m_formatBar->setVisible(m_editMode && isText);

    if (m_editMode && isText)
        openTextPanel();
    else
        closeTextPanel();

    DocumentView *dv = currentDocView();
    if (!dv) return;

    if (tool == QLatin1String("select"))
        dv->setTool(DocumentView::Tool::Select);
    else if (tool == QLatin1String("pan"))
        dv->setTool(DocumentView::Tool::Pan);
    else if (tool == QLatin1String("text"))
        dv->setTool(DocumentView::Tool::Text);
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
        if (m_translator.load(path)) {
            QApplication::installTranslator(&m_translator);
            qDebug() << "[i18n] translator loaded:" << path;
        } else {
            qWarning() << "[i18n] FAILED to load translator:" << path;
        }
    }

    m_appSettings->setLanguage(lang);
    m_appSettings->sync();

    retranslateUi();
}

void MainWindow::retranslateUi()
{
    m_topToolbar->retranslateUi();
    m_formatBar->retranslateUi();
    m_leftSidebar->retranslateUi();
    m_rightSidebar->retranslateUi();
    m_textPanel->retranslateUi();
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

void MainWindow::closeEvent(QCloseEvent *e)
{
    for (DocumentView *dv : m_docViews) {
        if (!confirmAndSave(dv)) {
            e->ignore();
            return;
        }
    }
    e->accept();
}

void MainWindow::openTextPanel()
{
    if (m_textPanelOpen) return;
    m_textPanelOpen = true;
    m_textPanel->setFixedWidth(TextPropertiesPanel::kWidth);
    m_textPanel->show();
}

void MainWindow::closeTextPanel()
{
    if (!m_textPanelOpen) return;
    m_textPanelOpen = false;
    m_textPanel->hide();
    m_textPanel->setFixedWidth(0);
}

bool MainWindow::confirmAndSave(DocumentView *dv)
{
    if (!dv || !dv->hasUnsavedEdits()) return true;

    const QString name = dv->currentFile().isEmpty()
        ? tr("Untitled")
        : QFileInfo(dv->currentFile()).fileName();

    const QMessageBox::StandardButton btn = QMessageBox::question(
        this,
        tr("Unsaved Changes"),
        tr("Save changes to \"%1\"?").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (btn == QMessageBox::Cancel) return false;

    if (btn == QMessageBox::Save) {
        if (!dv->currentFile().isEmpty())
            return dv->saveToFile(dv->currentFile());

        const QString path = QFileDialog::getSaveFileName(
            this, tr("Save PDF As"), {}, tr("PDF files (*.pdf)"));
        if (path.isEmpty()) return false;
        return dv->saveToFile(path);
    }

    return true; // Discard
}
