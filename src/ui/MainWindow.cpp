#include "MainWindow.hpp"

#include "PresentationWindow.hpp"
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
    connect(m_topToolbar, &TopToolbar::viewModeChanged, this, [this](bool grid) {
        if (DocumentView *dv = currentDocView())
            dv->setViewMode(grid ? DocumentView::ViewMode::Grid : DocumentView::ViewMode::Single);
    });

    // Left sidebar
    connect(m_leftSidebar, &LeftSidebar::toolSelected,        this, &MainWindow::onToolSelected);
    connect(m_leftSidebar, &LeftSidebar::settingsRequested,   this, [this]() {
        auto *panel = new SettingsPanel(m_appSettings, this);
        connect(panel, &SettingsPanel::themeChangeRequested,    this, &MainWindow::applyTheme);
        connect(panel, &SettingsPanel::languageChangeRequested, this, &MainWindow::applyLanguage);
        connect(panel, &SettingsPanel::shortcutsChanged,     this, &MainWindow::loadShortcuts);
        connect(panel, &SettingsPanel::zoomSettingsChanged, this, &MainWindow::loadZoomSettings);
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
    // FormatBar color → active editor live update
    connect(m_formatBar, &FormatBar::textColorChanged, this, [this](const QColor &c) {
        if (DocumentView *dv = currentDocView())
            dv->setEditorTextColor(c);
    });
    // FormatBar font family / bold / italic → active editor live update
    connect(m_formatBar, &FormatBar::fontFamilyChanged, this, [this](const QString &f) {
        if (DocumentView *dv = currentDocView())
            dv->setEditorFontFamily(f);
    });
    connect(m_formatBar, &FormatBar::boldToggled, this, [this](bool on) {
        if (DocumentView *dv = currentDocView())
            dv->setEditorBold(on);
    });
    connect(m_formatBar, &FormatBar::italicToggled, this, [this](bool on) {
        if (DocumentView *dv = currentDocView())
            dv->setEditorItalic(on);
    });

    // All keyboard shortcuts — created once here, sequences updated by loadShortcuts()
    struct Def { const char *key; void (MainWindow::*slot)(); };
    const Def defs[] = {
        { "save",   &MainWindow::onSave     },
        { "saveas", &MainWindow::onSaveAs   },
        { "print",  &MainWindow::onPrint    },
        { "open",   &MainWindow::onOpenFile },
        { "undo",   &MainWindow::onUndo     },
        { "redo",   &MainWindow::onRedo     },
        { "zoomin", &MainWindow::onZoomIn   },
        { "zoomout",&MainWindow::onZoomOut  },
    };
    for (const auto &d : defs) {
        auto *sc = new QShortcut(QKeySequence{}, this);
        connect(sc, &QShortcut::activated, this, d.slot);
        m_shortcuts.insert(QLatin1String(d.key), sc);
    }
    // Lambda-based shortcuts
    const auto addLambda = [&](const char *key, auto fn) {
        auto *sc = new QShortcut(QKeySequence{}, this);
        connect(sc, &QShortcut::activated, this, fn);
        m_shortcuts.insert(QLatin1String(key), sc);
    };
    addLambda("find",         [this]() { /* TODO: open find dialog */ });
    addLambda("texttool",     [this]() { onToolSelected(QStringLiteral("text")); });
    addLambda("comment",      [this]() { onToolSelected(QStringLiteral("comment")); });
    addLambda("presentation", [this]() { onStartPresentation(); });
    loadShortcuts();     // apply sequences from AppSettings (or defaults)
    loadZoomSettings();  // apply zoom settings from AppSettings (or defaults)

    // Status bar
    connect(m_statusBar, &StatusBar::previousPageRequested, this, [this]() {
        if (DocumentView *dv = currentDocView())
            dv->goToPage(dv->currentPage() - 1);
    });
    connect(m_statusBar, &StatusBar::nextPageRequested, this, [this]() {
        if (DocumentView *dv = currentDocView())
            dv->goToPage(dv->currentPage() + 1);
    });
    connect(m_statusBar, &StatusBar::pageRequested, this, [this](int page) {
        if (DocumentView *dv = currentDocView())
            dv->goToPage(page - 1);          // status bar counts from 1
    });
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
    dv->setZoomSettings(m_appSettings->zoomStep(), m_appSettings->ctrlWheelZoom(),
                        m_appSettings->zoomToPointer(), m_appSettings->wheelAction());
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

    // Keep the page indicator in step with scrolling / jumps in the view
    connect(dv, &DocumentView::pageChanged, this, [this, dv](int current, int total) {
        if (dv == currentDocView())
            m_statusBar->setPageInfo(current, total);
    });

    // Keep view-mode buttons in sync (e.g. when user clicks a grid card to return to single)
    connect(dv, &DocumentView::viewModeChanged, this, [this, dv](DocumentView::ViewMode mode) {
        if (dv == currentDocView())
            m_topToolbar->setViewMode(mode == DocumentView::ViewMode::Grid);
    });

    // Sync FormatBar font size ↔ active editor
    connect(dv, &DocumentView::editorFontSizeChanged,
            m_formatBar, &FormatBar::setFontSize);
    // Sync FormatBar font family/style with the detected font of the block
    // the user is editing (Acrobat-style: toolbar reflects the clicked text).
    connect(dv, &DocumentView::editorFontChanged, this,
            [this, dv](const QString &family, bool bold, bool italic) {
        if (dv != currentDocView()) return;
        m_formatBar->setFontFamily(family);
        m_formatBar->setBoldChecked(bold);
        m_formatBar->setItalicChecked(italic);
    });

    // Sync zoom label when user zooms via mouse wheel
    connect(dv, &DocumentView::zoomChanged, this, [this, dv](int percent) {
        if (dv == currentDocView()) {
            m_zoom = percent;
            m_topToolbar->setZoom(percent);
        }
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
    // Show where this tab was left off, not page 1.
    m_statusBar->setPageInfo(dv->currentPage() + 1,
                             dv->pageCount() > 0 ? dv->pageCount() : 1);
    m_topToolbar->setZoom(m_zoom);
    m_topToolbar->setViewMode(dv->viewMode() == DocumentView::ViewMode::Grid);
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
    openPath(path);
}

void MainWindow::openPath(const QString &path)
{
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

void MainWindow::onSaveAs()
{
    DocumentView *dv = currentDocView();
    if (!dv) return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save PDF As"), {}, tr("PDF files (*.pdf)"));
    if (!path.isEmpty())
        dv->saveToFile(path);
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

void MainWindow::onStartPresentation()
{
    DocumentView *dv = currentDocView();
    if (!dv || dv->currentFile().isEmpty()) return;
    auto *pw = new PresentationWindow(dv->currentFile(), dv->currentPage());
    pw->show();
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

void MainWindow::loadShortcuts()
{
    struct Def { const char *key; const char *defaultSeq; };
    static const Def kDefs[] = {
        { "save",     "Ctrl+S"       },
        { "saveas",   "Ctrl+Shift+S" },
        { "print",    "Ctrl+P"       },
        { "open",     "Ctrl+O"       },
        { "undo",     "Ctrl+Z"       },
        { "redo",     "Ctrl+Y"       },
        { "find",         "Ctrl+F"       },
        { "texttool",     "T"            },
        { "comment",      "C"            },
        { "zoomin",       "Ctrl++"       },
        { "zoomout",      "Ctrl+-"       },
        { "presentation", "F5"           },
    };
    for (const auto &def : kDefs) {
        const QString k = QLatin1String(def.key);
        if (!m_shortcuts.contains(k)) continue;
        const QKeySequence dflt = QKeySequence::fromString(
            QLatin1String(def.defaultSeq), QKeySequence::PortableText);
        m_shortcuts[k]->setKey(m_appSettings->shortcut(k, dflt));
    }
}

void MainWindow::loadZoomSettings()
{
    for (DocumentView *dv : m_docViews)
        dv->setZoomSettings(m_appSettings->zoomStep(), m_appSettings->ctrlWheelZoom(),
                            m_appSettings->zoomToPointer(), m_appSettings->wheelAction());
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
                    const QList<DocxPage> pages = dv->allPageContent();
                    const QString title = QFileInfo(dv->currentFile()).completeBaseName();
                    if (DocxExporter::exportToDocx(path, pages, title))
                        QMessageBox::information(this, tr("Export successful"),
                            tr("Document exported to \"%1\".").arg(QFileInfo(path).fileName()));
                    else
                        QMessageBox::warning(this, tr("Export failed"),
                            tr("Could not write to \"%1\".").arg(path));
                } else if (format == QLatin1String("image")) {
                    if (dv->exportPagesToImages(path, dlg.selectedImageQuality()))
                        QMessageBox::information(this, tr("Export successful"),
                            pages > 1
                                ? tr("%1 pages exported as PNG images.").arg(pages)
                                : tr("Document exported to \"%1\".").arg(QFileInfo(path).fileName()));
                    else
                        QMessageBox::warning(this, tr("Export failed"),
                            tr("Could not export PNG images to \"%1\".").arg(
                                QFileInfo(path).absolutePath()));
                } else {
                    if (dv->saveToFile(path))
                        QMessageBox::information(this, tr("Export successful"),
                            tr("Document exported to \"%1\".").arg(QFileInfo(path).fileName()));
                    else
                        QMessageBox::warning(this, tr("Export failed"),
                            tr("Could not write to \"%1\".").arg(path));
                }
            }
        }
    } else if (mode == QLatin1String("organize")) {
        DocumentView *dv = currentDocView();
        const QString file = dv ? dv->currentFile() : QString{};
        auto *dlg = new PdfOrganizerDialog(file, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::finished, this, [this, dv, dlg](int result) {
            if (result != QDialog::Accepted || !dv) return;
            const QString path = dlg->savedPath();
            if (!path.isEmpty())
                dv->openFile(path);
        });
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
