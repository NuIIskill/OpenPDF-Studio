#include "ui/MainWindow.hpp"

#include "ui/PresentationWindow.hpp"
#include "ui/DocumentView.hpp"
#include "ui/bookmarks/BookmarkPanel.hpp"
#include "ui/notes/NotesPanel.hpp"
#include "ui/bars/TopToolbar.hpp"
#include "ui/bars/FormatBar.hpp"
#include "ui/draw/DrawBar.hpp"
#include "ui/bars/StatusBar.hpp"
#include "ui/panels/LeftSidebar.hpp"
#include "ui/panels/ToolPanels.hpp"
#include "ui/panels/RightSidebar.hpp"
#include "ui/panels/TextPropertiesPanel.hpp"
#include "ui/settings/SettingsPanel.hpp"
#include "ui/organizer/PdfOrganizerDialog.hpp"
#include "ui/export/ExportDialog.hpp"
#include "ui/history/HistoryDialog.hpp"
#include "engine/edit/DocxExporter.hpp"
#include "engine/edit/PdfExporter.hpp"
#include "ui/theme/Theme.hpp"
#include "app/AppSettings.hpp"
#include "app/UpdateChecker.hpp"
#include "drm/LicenseNotice.hpp"

#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QCloseEvent>
#include <QKeySequence>
#include <QShortcut>

#ifdef HAVE_QT_PRINT
#include <QMarginsF>
#include <QPageLayout>
#include <QPageRanges>
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
    applyPanelLayout();

    QTimer::singleShot(0, this, &MainWindow::showLicenseNotices);
    QTimer::singleShot(0, this, &MainWindow::checkForUpdates);
}

SettingsPanel *MainWindow::openSettings()
{
    auto *panel = new SettingsPanel(m_appSettings, this);
    connect(panel, &SettingsPanel::themeChangeRequested,    this, &MainWindow::applyTheme);
    connect(panel, &SettingsPanel::languageChangeRequested, this, &MainWindow::applyLanguage);
    connect(panel, &SettingsPanel::shortcutsChanged,        this, &MainWindow::loadShortcuts);
    connect(panel, &SettingsPanel::zoomSettingsChanged,     this, &MainWindow::loadZoomSettings);
    connect(panel, &SettingsPanel::panelLayoutSettingChanged,
            this, &MainWindow::savePanelLayout);
    panel->open();
    return panel;
}

void MainWindow::showLicenseNotices()
{
    LicenseNotice::askUsageIfUnknown(this);
    LicenseNotice::showExpiryReminderIfDue(this, [this]() {
        openSettings()->showLicensePage();
    });
}

void MainWindow::checkForUpdates()
{
    if (!m_updateChecker) {
        m_updateChecker = new UpdateChecker(m_appSettings, this);
        connect(m_updateChecker, &UpdateChecker::finished, this,
                [this](const UpdateCheckResult &result) {

            if (!result.ok || !result.updateAvailable)
                return;

            auto *box = new QMessageBox(this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->setIcon(QMessageBox::Information);
            box->setWindowTitle(tr("Update available"));
            box->setText(tr("OpenPDF Studio %1 is available.").arg(result.latest));
            box->setInformativeText(tr("You are running %1.").arg(result.current));
            QPushButton *openBtn = box->addButton(tr("Open download page"),
                                                  QMessageBox::AcceptRole);
            box->addButton(tr("Later"), QMessageBox::RejectRole);
            box->setDefaultButton(openBtn);
            connect(box, &QMessageBox::finished, box, [box, openBtn]() {
                if (box->clickedButton() == openBtn)
                    QDesktopServices::openUrl(UpdateChecker::downloadPageUrl());
            });

            box->open();
        });
    }
    m_updateChecker->checkIfDue();
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

    m_formatBar = new FormatBar(central);
    m_formatBar->hide();
    root->addWidget(m_formatBar);

    m_drawBar = new DrawBar(central);
    m_drawBar->hide();
    root->addWidget(m_drawBar);

    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->setHandleWidth(1);
    m_splitter->setChildrenCollapsible(false);
    m_leftSidebar = new LeftSidebar(m_appSettings, m_splitter);
    m_bookmarkPanel = new BookmarkPanel(m_splitter);
    m_bookmarkPanel->hide();
    m_docStack    = new QStackedWidget(m_splitter);
    m_splitter->addWidget(m_leftSidebar);
    m_splitter->addWidget(m_bookmarkPanel);
    m_splitter->addWidget(m_docStack);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setStretchFactor(2, 1);

    m_textPanel    = new TextPropertiesPanel(central);
    m_notesPanel   = new NotesPanel(central);
    m_notesPanel->setDocumentAvailable(false);
    m_rightSidebar = new RightSidebar(central);
    m_textPanel->setFixedWidth(0);
    m_textPanel->hide();
    m_notesPanel->setFixedWidth(0);
    m_notesPanel->hide();

    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(m_splitter,     1);
    row->addWidget(m_textPanel,    0);
    row->addWidget(m_notesPanel,   0);
    m_toolPanels.insert(QStringLiteral("comment"), { m_notesPanel, 356 });

    for (const ToolPanels::Panel &def : ToolPanels::all()) {
        QWidget *panel = def.create(central);
        if (!panel) continue;
        panel->setFixedWidth(0);
        panel->hide();
        row->addWidget(panel, 0);
        m_toolPanels.insert(def.toolId, { panel, def.width });
    }

    row->addWidget(m_rightSidebar, 0);
    root->addLayout(row, 1);

    m_statusBar = new StatusBar(central);
    root->addWidget(m_statusBar);

    addDocView();
}

void MainWindow::connectSignals()
{

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

    connect(m_leftSidebar, &LeftSidebar::toolSelected,        this, &MainWindow::onToolSelected);
    connect(m_leftSidebar, &LeftSidebar::settingsRequested, this, [this]() { openSettings(); });
    connect(m_bookmarkPanel, &BookmarkPanel::closeRequested, this, [this]() {
        onToolSelected(QStringLiteral("select"));
    });
    connect(m_bookmarkPanel, &BookmarkPanel::pageRequested, this, [this](int page) {
        if (DocumentView *dv = currentDocView()) dv->goToPage(page);
    });
    connect(m_bookmarkPanel, &BookmarkPanel::bookmarksEdited, this,
            [this](const QList<PdfBookmark> &bookmarks) {
        if (DocumentView *dv = currentDocView()) dv->setBookmarks(bookmarks);
    });
    connect(m_notesPanel, &NotesPanel::closeRequested, this, [this]() {
        onToolSelected(QStringLiteral("select"));
    });
    connect(m_notesPanel, &NotesPanel::newNoteRequested, this, [this]() {
        if (DocumentView *dv = currentDocView()) dv->createNote();
    });
    connect(m_notesPanel, &NotesPanel::noteSelected, this, [this](const QString &id) {
        if (DocumentView *dv = currentDocView()) dv->selectNote(id);
    });
    connect(m_notesPanel, &NotesPanel::saveRequested, this,
            [this](const QString &id, const QString &title, const QString &text) {
        if (DocumentView *dv = currentDocView()) dv->updateNote(id, title, text);
    });
    connect(m_notesPanel, &NotesPanel::deleteRequested, this, [this](const QString &id) {
        if (DocumentView *dv = currentDocView()) dv->deleteNote(id);
    });
    connect(m_notesPanel, &NotesPanel::pinRequested, this,
            [this](const QString &id, bool pinned) {
        if (DocumentView *dv = currentDocView()) dv->setNotePinned(id, pinned);
    });

    connect(m_rightSidebar, &RightSidebar::modeSelected, this, &MainWindow::onModeSelected);

    connect(m_textPanel, &TextPropertiesPanel::propertiesChanged, this,
            [this](const TextBoxProperties &properties) {
        if (DocumentView *dv = currentDocView())
            dv->setTextBoxProperties(properties);
    });

    connect(m_formatBar, &FormatBar::fontSizeChanged, this, [this](int pt) {
        if (DocumentView *dv = currentDocView())
            dv->setEditorFontSize(pt);
    });

    connect(m_formatBar, &FormatBar::textColorChanged, this, [this](const QColor &c) {
        if (DocumentView *dv = currentDocView())
            dv->setEditorTextColor(c);
    });

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
    connect(m_formatBar, &FormatBar::underlineToggled, this, [this](bool on) {
        if (DocumentView *dv = currentDocView())
            dv->setEditorUnderline(on);
    });
    connect(m_formatBar, &FormatBar::alignmentChanged, this, [this](Qt::Alignment a) {
        if (DocumentView *dv = currentDocView()) dv->setEditorAlignment(a);
    });
    connect(m_formatBar, &FormatBar::listStyleChanged, this,
            [this](TextBoxProperties::ListStyle style) {
        if (DocumentView *dv = currentDocView()) dv->setEditorListStyle(style);
    });
    connect(m_formatBar, &FormatBar::indentChanged, this, [this](int delta) {
        if (DocumentView *dv = currentDocView()) dv->changeEditorIndent(delta);
    });
    connect(m_formatBar, &FormatBar::lineSpacingChanged, this, [this](double multiplier) {
        if (DocumentView *dv = currentDocView()) dv->setEditorLineSpacing(multiplier);
    });
    connect(m_formatBar, &FormatBar::advancedToggled, this, [this](bool on) {
        if (on) openTextPanel(); else closeTextPanel();
    });

    connect(m_drawBar, &DrawBar::toolChanged, this, [this](DrawTool tool) {
        if (DocumentView *dv = currentDocView()) dv->setDrawTool(tool);
    });
    connect(m_drawBar, &DrawBar::widthChanged, this, [this](qreal widthPt) {
        if (DocumentView *dv = currentDocView()) dv->setDrawWidth(widthPt);
    });
    connect(m_drawBar, &DrawBar::colorChanged, this, [this](const QColor &color) {
        if (DocumentView *dv = currentDocView()) dv->setDrawColor(color);
    });
    /// Maps a shortcut key to a MainWindow action.
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

    const auto addLambda = [&](const char *key, auto fn) {
        auto *sc = new QShortcut(QKeySequence{}, this);
        connect(sc, &QShortcut::activated, this, fn);
        m_shortcuts.insert(QLatin1String(key), sc);
    };
    addLambda("find",         [this]() {
        if (DocumentView *dv = currentDocView()) dv->openFind();
    });
    addLambda("texttool",     [this]() { onToolSelected(QStringLiteral("text")); });
    addLambda("comment",      [this]() { onToolSelected(QStringLiteral("comment")); });
    addLambda("presentation", [this]() { onStartPresentation(); });
    loadShortcuts();
    loadZoomSettings();

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
            dv->goToPage(page - 1);
    });
    connect(m_statusBar, &StatusBar::panelToggleRequested,  this, [this]() {
        setRightSidebarCollapsed(!m_rightSidebarCollapsed);

        savePanelLayout();
    });
}

DocumentView *MainWindow::addDocView()
{
    auto *dv = new DocumentView(m_docStack);
    dv->setTextBoxDefaults(m_textPanel->defaultProperties());
    dv->setZoomSettings(m_appSettings->zoomStep(), m_appSettings->ctrlWheelZoom(),
                        m_appSettings->zoomToPointer(), m_appSettings->wheelAction());
    m_docViews.append(dv);
    m_docStack->addWidget(dv);

    const int idx = m_topToolbar->addTab();

    connect(dv, &DocumentView::pdfDropped, this, &MainWindow::openPath);

    connect(dv, &DocumentView::fileOpened, this, [this, dv](const QString &path, int pages) {
        const int i = m_docViews.indexOf(dv);
        if (i >= 0) {
            const QFileInfo fi(path);
            m_topToolbar->setTabLabel(i, fi.fileName());
        }
        m_statusBar->setPageInfo(1, pages);
        if (dv == currentDocView()) m_notesPanel->setDocumentAvailable(pages > 0);
        if (dv == currentDocView()) refreshBookmarkPanel();
    });

    connect(dv, &DocumentView::pageChanged, this, [this, dv](int current, int total) {
        if (dv == currentDocView()) {
            m_statusBar->setPageInfo(current, total);
            m_bookmarkPanel->setCurrentPage(current - 1);
        }
    });

    connect(dv, &DocumentView::viewModeChanged, this, [this, dv](DocumentView::ViewMode mode) {
        if (dv == currentDocView())
            m_topToolbar->setViewMode(mode == DocumentView::ViewMode::Grid);
    });

    connect(dv, &DocumentView::editorFontSizeChanged,
            m_formatBar, &FormatBar::setFontSize);

    connect(dv, &DocumentView::editorFontChanged, this,
            [this, dv](const QString &family, bool bold, bool italic,
                       bool underline) {
        if (dv != currentDocView()) return;
        m_formatBar->setFontFamily(family);
        m_formatBar->setBoldChecked(bold);
        m_formatBar->setItalicChecked(italic);
        m_formatBar->setUnderlineChecked(underline);
    });
    connect(dv, &DocumentView::textBoxPropertiesChanged, this,
            [this, dv](const TextBoxProperties &properties) {
        if (dv == currentDocView()) {
            m_textPanel->setProperties(properties);
            m_formatBar->setAlignment(properties.horizontalAlign);
            if (properties.lineSpacingMultiplier > 0.0)
                m_formatBar->setLineSpacing(properties.lineSpacingMultiplier);
        }
    });
    connect(dv, &DocumentView::textBoxEditingChanged, this,
            [this, dv](bool active) {
        if (dv == currentDocView())
            m_textPanel->setEditorActive(active);
    });
    connect(dv, &DocumentView::notesChanged, this,
            [this, dv](const QList<NoteData> &notes) {
        if (dv == currentDocView()) m_notesPanel->setNotes(notes);
    });
    connect(dv, &DocumentView::noteSelected, this,
            [this, dv](const QString &id) {
        if (dv == currentDocView()) m_notesPanel->setSelectedNote(id);
    });

    connect(dv, &DocumentView::zoomChanged, this, [this, dv](int percent) {
        if (dv == currentDocView()) {
            m_zoom = percent;
            m_topToolbar->setZoom(percent);
        }
    });

    m_docStack->setCurrentWidget(dv);
    m_topToolbar->setCurrentTab(idx);
    m_notesPanel->setNotes(dv->notes());
    m_notesPanel->setDocumentAvailable(dv->pageCount() > 0);
    return dv;
}

DocumentView *MainWindow::currentDocView() const
{
    return qobject_cast<DocumentView *>(m_docStack->currentWidget());
}

void MainWindow::onNewTab()
{
    addDocView();
    if (DocumentView *dv = currentDocView()) dv->setEditMode(m_editMode);
    onToolSelected(m_activeTool);
    refreshBookmarkPanel();
}

void MainWindow::onTabActivated(int index)
{
    if (index < 0 || index >= m_docViews.size()) return;
    m_docStack->setCurrentWidget(m_docViews[index]);
    m_topToolbar->setCurrentTab(index);

    const DocumentView *dv = m_docViews[index];

    m_statusBar->setPageInfo(dv->currentPage() + 1,
                             dv->pageCount() > 0 ? dv->pageCount() : 1);
    m_topToolbar->setZoom(m_zoom);
    m_topToolbar->setViewMode(dv->viewMode() == DocumentView::ViewMode::Grid);
    refreshBookmarkPanel();
    m_notesPanel->setNotes(dv->notes());
    m_notesPanel->setDocumentAvailable(dv->pageCount() > 0);
    onToolSelected(m_activeTool);
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
        m_notesPanel->setDocumentAvailable(false);
        refreshBookmarkPanel();
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
        m_notesPanel->setNotes(m_docViews[next]->notes());
        m_notesPanel->setDocumentAvailable(m_docViews[next]->pageCount() > 0);
        onToolSelected(m_activeTool);
        refreshBookmarkPanel();
    }
}

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

bool MainWindow::saveDocument(DocumentView *dv, const QString &path)
{
    if (!dv || path.isEmpty()) return false;
    if (dv->saveToFile(path)) return true;

    QMessageBox::warning(
        this, tr("Save failed"),
        tr("Could not write \"%1\".\n\nThe file may be write-protected or "
           "open in another program. The document is unchanged — try saving "
           "it under a different name.")
            .arg(QFileInfo(path).fileName()));
    return false;
}

void MainWindow::onSave()
{
    DocumentView *dv = currentDocView();
    if (!dv) return;

    if (!dv->currentFile().isEmpty()) {
        saveDocument(dv, dv->currentFile());
    } else {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Save PDF As"), {}, tr("PDF files (*.pdf)"));
        if (!path.isEmpty())
            saveDocument(dv, path);
    }
}

void MainWindow::onSaveAs()
{
    DocumentView *dv = currentDocView();
    if (!dv) return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save PDF As"), {}, tr("PDF files (*.pdf)"));
    if (!path.isEmpty())
        saveDocument(dv, path);
}

void MainWindow::onPrint()
{
#ifdef HAVE_QT_PRINT
    DocumentView *dv = currentDocView();
    if (!dv || dv->contentFile().isEmpty()) return;
    const int pageCount = dv->pageCount();
    if (pageCount <= 0) return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(QFileInfo(dv->currentFile()).completeBaseName());

    printer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    printer.setFromTo(1, pageCount);

    QPrintDialog dlg(&printer, this);
    dlg.setOption(QAbstractPrintDialog::PrintPageRange);
    dlg.setOption(QAbstractPrintDialog::PrintCurrentPage);
    dlg.setWindowTitle(tr("Print"));
    if (dlg.exec() != QDialog::Accepted) return;

    QList<int> pages;
    switch (printer.printRange()) {
    case QPrinter::CurrentPage:
        pages.append(dv->currentPage());
        break;
    case QPrinter::PageRange: {

        const QPageRanges ranges = printer.pageRanges();
        if (!ranges.isEmpty()) {
            for (int p = ranges.firstPage(); p <= ranges.lastPage(); ++p)
                if (ranges.contains(p)) pages.append(p - 1);
        } else {
            for (int p = printer.fromPage(); p <= printer.toPage(); ++p)
                pages.append(p - 1);
        }
        break;
    }
    default:
        break;
    }

    const int copies = printer.supportsMultipleCopies()
        ? 1 : qMax(1, printer.copyCount());
    if (copies > 1) {
        if (pages.isEmpty())
            for (int p = 0; p < pageCount; ++p) pages.append(p);
        const QList<int> once = pages;
        pages.clear();
        if (printer.collateCopies()) {
            for (int c = 0; c < copies; ++c) pages.append(once);
        } else {
            for (int page : once)
                for (int c = 0; c < copies; ++c) pages.append(page);
        }
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = dv->printDocument(&printer, pages);
    QApplication::restoreOverrideCursor();

    if (!ok)
        QMessageBox::warning(this, tr("Print"),
                             tr("The document could not be printed."));
#endif
}

void MainWindow::onUndo()
{
    if (DocumentView *dv = currentDocView())
        dv->undo();
}

void MainWindow::onRedo()
{
    if (DocumentView *dv = currentDocView())
        dv->redo();
}

void MainWindow::onStartPresentation()
{
    DocumentView *dv = currentDocView();
    if (!dv || dv->contentFile().isEmpty()) return;

    auto *pw = new PresentationWindow(dv->contentFile(), dv->currentPage());
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
        { "comment",      "N"            },
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

void MainWindow::onModeSelected(const QString &mode)
{
    if (mode == QLatin1String("edit")) {
        m_editMode = !m_editMode;
        for (DocumentView *dv : m_docViews)
            dv->setEditMode(m_editMode);
        m_rightSidebar->setMode(m_editMode ? QStringLiteral("edit") : QString{});
        if (!m_editMode) {
            closeTextPanel();
            m_formatBar->setAdvancedChecked(false);
            m_formatBar->hide();
            m_drawBar->hide();
            onToolSelected(QStringLiteral("select"));
        }
    } else if (mode == QLatin1String("export")) {
        DocumentView *dv   = currentDocView();
        const QString file = dv ? dv->currentFile() : QString{};
        const int pageCount = dv ? dv->pageCount() : 1;
        ExportDialog dlg(file, pageCount, dv ? dv->currentPage() : 0, this);
        if (dlg.exec() == QDialog::Accepted && dv)
            runExport(dv, dlg.request());
    } else if (mode == QLatin1String("organize")) {
        DocumentView *dv = currentDocView();

        auto *dlg = new PdfOrganizerDialog(dv ? dv->contentFile() : QString{}, this);
        if (dv) dlg->setTargetPath(dv->currentFile());
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::finished, this, [this, dv, dlg](int result) {
            if (result != QDialog::Accepted || !dv) return;
            const QString path = dlg->resultPath();
            if (path.isEmpty()) return;
            if (dlg->resultIsWorkingCopy())
                dv->openWorkingCopy(path, dlg->targetPath(), dlg->appliedChange());
            else
                dv->openFile(path);
        });
        dlg->open();
    } else if (mode == QLatin1String("history")) {
        openHistoryDialog();
    }
}

void MainWindow::openHistoryDialog()
{
    DocumentView *dv = currentDocView();
    if (!dv || dv->contentFile().isEmpty()) {
        QMessageBox::information(this, tr("Change history"),
                                 tr("Open a document to see its change history."));
        m_rightSidebar->setMode(m_editMode ? QStringLiteral("edit") : QString{});
        return;
    }

    m_rightSidebar->setMode(QStringLiteral("history"));

    auto *dlg = new HistoryDialog(dv->history(),
                                  QFileInfo(dv->currentFile()).fileName(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    const auto syncButtons = [dv, dlg]() {
        dlg->setUndoRedoAvailable(dv->undoStack()->canUndo(),
                                  dv->undoStack()->canRedo());
    };
    syncButtons();
    connect(dv->undoStack(), &QUndoStack::canUndoChanged, dlg, syncButtons);
    connect(dv->undoStack(), &QUndoStack::canRedoChanged, dlg, syncButtons);

    connect(dlg, &HistoryDialog::undoRequested, this, &MainWindow::onUndo);
    connect(dlg, &HistoryDialog::redoRequested, this, &MainWindow::onRedo);
    connect(dlg, &HistoryDialog::clearRequested, dv, [dv]() {
        dv->history()->clear();
    });
    connect(dlg, &HistoryDialog::restoreRequested, this, [this, dv, dlg](int index) {
        if (dv->restoreHistoryState(index)) return;
        QMessageBox::warning(dlg, tr("Change history"),
                             tr("This state could not be restored — the copy of "
                                "the document it was kept in is no longer there."));
    });
    connect(dlg, &QDialog::finished, this, [this]() {
        m_rightSidebar->setMode(m_editMode ? QStringLiteral("edit") : QString{});
    });
    dlg->open();
}

void MainWindow::runExport(DocumentView *dv, const ExportRequest &req)
{
    if (!dv || req.path.isEmpty()) return;

    const QString shownName = QFileInfo(req.path).fileName();
    bool ok = false;
    QString failure;

    if (req.format == QLatin1String("word")) {
        const QList<DocxPage> content = dv->allPageContent(req.pages);
        const QString title = QFileInfo(dv->currentFile()).completeBaseName();
        DocxExportOptions docxOpt;
        docxOpt.compressImages = req.compressImages;
        docxOpt.imageQuality   = req.imageQuality;
        ok = DocxExporter::exportToDocx(req.path, content, title, docxOpt);
        failure = tr("Could not write to \"%1\".").arg(req.path);

    } else if (req.format == QLatin1String("image")) {
        ok = dv->exportPagesToImages(req.path, req.imageQuality, req.pages);
        failure = tr("Could not export PNG images to \"%1\".")
                      .arg(QFileInfo(req.path).absolutePath());

    } else {

        const QString source = dv->contentFile();
        PdfExportOptions opt;
        opt.pages           = req.pages;
        opt.includeComments = req.includeComments;
        opt.keepForms       = req.keepForms;
        opt.embedFonts      = req.embedFonts;
        opt.compressImages  = req.compressImages;
        opt.imageQuality    = req.imageQuality;
        opt.userPassword    = req.password;

        const bool plainRequest = req.pages.size() == dv->pageCount()
                               && req.includeComments && req.keepForms
                               && req.embedFonts && req.password.isEmpty();
        ok = pdfExportAvailable() && exportPdf(source, req.path, opt);
        if (!ok && plainRequest) ok = dv->saveToFile(req.path);
        failure = ok ? QString{}
                     : pdfExportAvailable()
                         ? tr("Could not write \"%1\".").arg(shownName)

                         : tr("Could not write \"%1\".\n\n"
                              "Selecting pages or setting a password for a PDF "
                              "needs qpdf, which this build does not include. "
                              "Exporting as Word or PNG is unaffected.")
                               .arg(shownName);
    }

    if (!ok) {
        QMessageBox::warning(this, tr("Export failed"), failure);
        return;
    }

    const int pages = req.pages.size();
    QMessageBox::information(this, tr("Export successful"),
        req.format == QLatin1String("image") && pages > 1
            ? tr("%1 pages exported as PNG images.").arg(pages)
            : tr("Document exported to \"%1\".").arg(shownName));

    if (req.openAfterExport) {

        const bool many = req.format == QLatin1String("image") && pages > 1;
        const QString target = many ? QFileInfo(req.path).absolutePath() : req.path;
        QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    }
}

void MainWindow::onToolSelected(const QString &tool)
{

    const auto needsEditMode = [](const QString &id) {
        for (const ToolDef &t : LeftSidebar::toolCatalog())
            if (t.id == id) return t.needsEditMode;
        return false;
    };

    if (needsEditMode(tool) && !m_editMode) {
        const auto ans = QMessageBox::question(
            this,
            tr("Edit Mode"),
            tr("Do you want to enable Edit Mode?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);

        if (ans == QMessageBox::No) {
            m_activeTool = QStringLiteral("select");
            m_leftSidebar->setActiveTool(m_activeTool);
            m_bookmarkPanel->hide();
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

    m_leftSidebar->setActiveTool(tool);

    const bool showBookmarks = (tool == QLatin1String("bookmark"));
    m_bookmarkPanel->setVisible(showBookmarks);
    if (showBookmarks) refreshBookmarkPanel();

    for (auto it = m_toolPanels.cbegin(); it != m_toolPanels.cend(); ++it) {
        const bool wanted = (it.key() == tool);
        it.value().widget->setFixedWidth(wanted ? it.value().width : 0);
        it.value().widget->setVisible(wanted);
    }

    const bool isText = (tool == QLatin1String("text"));
    const bool isDraw = (tool == QLatin1String("draw"));
    m_formatBar->setVisible(m_editMode && isText);
    m_drawBar->setVisible(m_editMode && isDraw);

    if (!(m_editMode && isText)) {
        closeTextPanel();
        m_formatBar->setAdvancedChecked(false);
    }

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
    else if (tool == QLatin1String("comment"))
        dv->setTool(DocumentView::Tool::Comment);
    else if (tool == QLatin1String("draw")) {
        dv->setDrawTool(m_drawBar->currentTool());
        dv->setDrawColor(m_drawBar->currentColor());
        dv->setDrawWidth(m_drawBar->currentWidth());
        dv->setTool(DocumentView::Tool::Draw);
    }
    else if (tool == QLatin1String("attach"))
        dv->setTool(DocumentView::Tool::Attach);
    else if (tool == QLatin1String("bookmark"))
        dv->setTool(DocumentView::Tool::Select);
    else
        dv->setTool(DocumentView::Tool::Select);

    dv->setActiveToolId(tool);
}

void MainWindow::applyTheme(const QString &mode)
{
    Theme::apply(mode);
    m_topToolbar->refreshTheme();
    m_leftSidebar->refreshTheme();
    m_bookmarkPanel->refreshTheme();
    m_notesPanel->refreshTheme();
    m_rightSidebar->refreshTheme();
    m_statusBar->refreshTheme();
    m_formatBar->refreshTheme();
    m_drawBar->refreshTheme();
    for (DocumentView *dv : m_docViews)
        dv->refreshTheme();
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
    m_drawBar->retranslateUi();
    m_leftSidebar->retranslateUi();
    m_bookmarkPanel->retranslateUi();
    m_notesPanel->retranslateUi();
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
    savePanelLayout();
    e->accept();
}

void MainWindow::setRightSidebarCollapsed(bool collapsed)
{
    m_rightSidebarCollapsed = collapsed;
    if (collapsed) {
        m_rightSidebar->hide();
        m_rightSidebar->setFixedWidth(0);
    } else {
        m_rightSidebar->setFixedWidth(RightSidebar::kWidth);
        m_rightSidebar->show();
    }
}

void MainWindow::applyPanelLayout()
{
    if (!m_appSettings || !m_appSettings->preservePanelLayout())
        return;

    setRightSidebarCollapsed(m_appSettings->rightPanelCollapsed());

    const QByteArray splitter = m_appSettings->splitterState();
    if (!splitter.isEmpty())
        m_splitter->restoreState(splitter);
}

void MainWindow::savePanelLayout()
{
    if (!m_appSettings) return;

    if (!m_appSettings->preservePanelLayout())
        return;

    m_appSettings->setRightPanelCollapsed(m_rightSidebarCollapsed);
    m_appSettings->setSplitterState(m_splitter->saveState());
    m_appSettings->sync();
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

void MainWindow::refreshBookmarkPanel()
{
    DocumentView *dv = currentDocView();
    if (!dv) {
        m_bookmarkPanel->setDocument({}, 0, false);
        return;
    }
    m_bookmarkPanel->setDocument(dv->bookmarks(), dv->pageCount(),
                                 dv->bookmarkEditingAvailable());
    m_bookmarkPanel->setCurrentPage(dv->currentPage());
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
            return saveDocument(dv, dv->currentFile());

        const QString path = QFileDialog::getSaveFileName(
            this, tr("Save PDF As"), {}, tr("PDF files (*.pdf)"));
        if (path.isEmpty()) return false;
        return saveDocument(dv, path);
    }

    return true;
}
