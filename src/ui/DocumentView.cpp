#include "ui/DocumentView.hpp"

#include "app/PdfPwStore.hpp"
#include "engine/document/DocumentSource.hpp"
#include "engine/edit/InkMetrics.hpp"
#include "app/SafeWrite.hpp"
#include "app/SessionStore.hpp"
#include "ui/tools/ImageAnnotation.hpp"
#include "ui/view/ImageAnnotationLayer.hpp"
#include "ui/view/LinkAnnotationLayer.hpp"
#include "ui/notes/NoteLayer.hpp"
#include "ui/draw/DrawingLayer.hpp"
#include "ui/view/HoverHighlight.hpp"
#include "ui/view/FindController.hpp"
#include "ui/view/PageLayoutEngine.hpp"
#include "ui/view/PageOverlay.hpp"
#include "ui/view/ZoomController.hpp"
#include "ui/view/TextSelectionController.hpp"
#include "ui/widgets/PasswordDialog.hpp"

#include <QFileInfo>

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <cstring>
#endif


#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QRubberBand>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QCursor>
#include <QScrollBar>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPalette>
#include <QPainter>
#include <QApplication>
#include <QClipboard>
#include <QTextEdit>
#include <QStyle>
#include <QTimer>
#include <QDebug>
#include <QMap>
#include <QFileDialog>
#include <QFontMetrics>
#include <QMenu>
#include <QRegularExpression>
#include <QUndoCommand>
#include <QStringList>
#include <QKeyEvent>
#include <algorithm>
#include <limits>

void DocumentView::setZoom(int percent)
{
    m_zoomCtl->setZoom(percent);
}

void DocumentView::setZoomSettings(int step, bool ctrlWheel, bool toPointer,
                                   const QString &wheelAction)
{
    m_zoomCtl->setSettings(step, ctrlWheel, toPointer, wheelAction);
}

void DocumentView::wheelEvent(QWheelEvent *e)
{
    if (!m_zoomCtl->handleWheel(e)) QScrollArea::wheelEvent(e);
}

// Everything anchored to a page has to follow a zoom: the selection highlights
// and, while an edit is open, the editor frame.
void DocumentView::repositionForZoom()
{
    // Page labels can be re-laid out once more after this, so the highlights
    // are repositioned again when that layout has settled.
    m_selection->relayout();
    m_linkLayer->relayout();
    m_noteLayer->relayout();
    m_drawingLayer->relayout();
    QTimer::singleShot(0, this, [this]() { m_selection->relayout(); });
    repositionEditorFrame();
}

void DocumentView::repositionEditorFrame()
{
#ifdef HAVE_PDF_RENDERING
    // The blank that hides the original text sticks to its page inside the
    // layout engine, so a re-render at the new zoom keeps it — only the editor
    // frame has to follow.
    if (m_edit.activeEditPage < 0 || !m_editorFrame->isVisible()) return;
    // Reposition the editor frame for the new zoom.  A 0 ms timer defers the
    // reposition until after the layout has settled — the frame may still be
    // growing to the text it holds.
    const int activePage = m_edit.activeEditPage;
    QTimer::singleShot(0, this, [this, activePage]() {
        if (m_edit.activeEditPage != activePage || !m_editorFrame->isVisible()) return;
        // Force the canvas layout NOW — label positions are stale until the
        // deferred relayout has run, and the 0 ms timer can fire first.
        if (m_layout) m_layout->activate();
        const QLabel *lbl = pageLabel(activePage);
        if (!lbl) return;
        // Read the CURRENT zoom, not a captured one: rapid wheel zooming
        // queues several of these lambdas and each must position for the
        // zoom the page is actually rendered at.
        const qreal scale = PdfRenderer::screenScale(m_zoomCtl->zoom());
        m_editorFrame->setPageRect(lbl->geometry());  // page rect changes with zoom
        const QRectF cb(m_edit.activeEditBounds.topLeft() * scale + QPointF(lbl->pos()),
                        m_edit.activeEditBounds.size() * scale);
        m_editorFrame->repositionForZoom(
            cb, qMax(1.0, m_edit.currentEditorRenderSizePt * scale),
            m_edit.currentBox, scale);
    });
#endif
}

void DocumentView::setTool(Tool tool)
{
    if (tool != Tool::Select) m_selection->clear();
    m_tool = tool;

    // Image annotations are interactive only while the image tool is active.
    m_imageLayer->setToolActive(tool == Tool::Image);
    m_linkLayer->setToolActive(tool == Tool::Attach);
    m_noteLayer->setToolActive(tool == Tool::Comment);
    m_drawingLayer->setActive(m_editMode && tool == Tool::Draw);

    switch (tool) {
    case Tool::Pan:    viewport()->setCursor(Qt::OpenHandCursor);    break;
    case Tool::Text:   viewport()->setCursor(Qt::IBeamCursor);       break;
    case Tool::Select: viewport()->setCursor(Qt::IBeamCursor);       break;   // marks text
    case Tool::Image:
        viewport()->setCursor(Qt::CrossCursor);
        m_imageLayer->scanVisiblePage(firstVisiblePage());
        break;
    case Tool::Attach:
        viewport()->setCursor(Qt::IBeamCursor);
        break;
    case Tool::Comment:
    case Tool::Draw:
        viewport()->setCursor(Qt::CrossCursor);
        break;
    default:           viewport()->setCursor(Qt::ArrowCursor);       break;
    }
}

void DocumentView::setActiveToolId(const QString &toolId)
{
    for (PageOverlay *overlay : std::as_const(m_overlays))
        overlay->setActiveTool(toolId);
}

QString DocumentView::selectedText() const
{
    return m_selection->selectedText();
}

void DocumentView::copySelectedText()
{
    m_selection->copyToClipboard();
}

void DocumentView::openFind()
{
    m_find->open();
}

void DocumentView::setDrawTool(DrawTool tool)
{
    m_drawingLayer->setTool(tool);
}

void DocumentView::setDrawColor(const QColor &color)
{
    m_drawingLayer->setColor(color);
}

void DocumentView::setDrawWidth(qreal widthPt)
{
    m_drawingLayer->setWidth(widthPt);
}

QList<NoteData> DocumentView::notes() const
{
    return m_noteLayer ? m_noteLayer->notes() : QList<NoteData>{};
}

void DocumentView::createNote()
{
    if (m_noteLayer && pageCount() > 0)
        m_noteLayer->addAtPageCenter(currentPage());
}

void DocumentView::selectNote(const QString &id)
{
    if (m_noteLayer) m_noteLayer->activate(id);
}

void DocumentView::updateNote(const QString &id, const QString &title,
                              const QString &text)
{
    if (m_noteLayer) m_noteLayer->update(id, title, text);
}

void DocumentView::deleteNote(const QString &id)
{
    if (m_noteLayer) m_noteLayer->remove(id);
}

void DocumentView::setNotePinned(const QString &id, bool pinned)
{
    if (m_noteLayer) m_noteLayer->setPinned(id, pinned);
}

// ── Editor font state (FormatBar sync) ────────────────────────────────────────

int DocumentView::currentPage() const
{
    if (pageLabelCount() == 0) return 0;

    // The page covering most of the viewport is the one the user is reading.
    // Taking the first partially visible page instead would keep the indicator
    // one page behind for as long as a sliver of it hangs in at the top.
    const int top    = verticalScrollBar()->value();
    const int bottom = top + viewport()->height();

    int best = 0;
    int bestVisible = -1;
    for (int i = 0; i < pageLabelCount(); ++i) {
        const int y0 = pageLabel(i)->pos().y();
        const int y1 = y0 + pageLabel(i)->height();
        if (y0 >= bottom) break;

        const int visible = qMin(y1, bottom) - qMax(y0, top);
        if (visible > bestVisible) {
            bestVisible = visible;
            best        = i;
        }
    }
    return best;
}

int DocumentView::firstVisiblePage() const
{
    for (int i = 0; i < pageLabelCount(); ++i)
        if (pageLabel(i)->pos().y() >= verticalScrollBar()->value())
            return i;
    return 0;
}

void DocumentView::goToPage(int page)
{
    scrollToPage(page, true);
}

void DocumentView::scrollToPage(int page, bool allowRetry)
{
    if (pageLabelCount() == 0) return;
    page = qBound(0, page, pageLabelCount() - 1);

    // The page widgets live on m_canvas, which is not the widget on screen in
    // grid mode — jumping to a page there means returning to the page view.
    if (m_viewMode == ViewMode::Grid)
        setViewMode(ViewMode::Single);

    // Right after open/zoom the layout can still be pending, in which case
    // every page would report pos().y() == 0 and the jump would go nowhere.
    if (m_layout) m_layout->activate();

    constexpr int kTopGap = 20;   // leave a little air above the page
    const int target = qMax(0, pageLabel(page)->pos().y() - kTopGap);
    verticalScrollBar()->setValue(target);

    if (allowRetry && verticalScrollBar()->value() != target) {
        // The scroll area sizes its canvas — and with it the scrollbar range —
        // only once it has processed the new page widgets. Until then the jump
        // is clamped to 0, which is what made the very first click on the page
        // arrows after opening a document do nothing. Retry once, after that.
        QTimer::singleShot(0, this, [this, page]() { scrollToPage(page, false); });
        return;
    }
    reportCurrentPage();
}

void DocumentView::reportCurrentPage()
{
    if (m_src->pageCount() <= 0 || pageLabelCount() == 0) return;
    const int page = currentPage();
    if (page == m_lastReportedPage) return;
    m_lastReportedPage = page;
    Q_EMIT pageChanged(page + 1, m_src->pageCount());
}

void DocumentView::retranslateUi()
{
#ifdef HAVE_PDF_RENDERING
    m_dropHint->setText(tr("Drop a PDF here or click a tab to open"));
#else
    m_dropHint->setText(tr(
        "PDF rendering is not available in this build.\n"
        "Please use a build that includes Qt6::Pdf support.\n"
        "(See build-win.sh for instructions.)"));
#endif
    if (m_find) m_find->retranslateUi();
}

void DocumentView::refreshTheme()
{
    if (m_find) m_find->refreshTheme();
}

void DocumentView::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QScrollArea::changeEvent(e);
}

void DocumentView::keyPressEvent(QKeyEvent *e)
{
    // Copy marked page text. Editors are QTextEdit children and consume their
    // own Ctrl+C before it ever reaches the view, so both paths coexist.
    if (e->matches(QKeySequence::Copy) && m_selection->hasSelection()) {
        copySelectedText();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Escape && m_selection->hasSelection()) {
        m_selection->clear();
        e->accept();
        return;
    }
    QScrollArea::keyPressEvent(e);
}

void DocumentView::resizeEvent(QResizeEvent *e)
{
    QScrollArea::resizeEvent(e);
    if (m_find) m_find->relayout();
    if (m_viewMode == ViewMode::Grid) {
        m_layoutEngine->relayoutGrid(viewport()->width());
        return;
    }
    syncVisibleRect();
    // The canvas is CENTRED in the viewport: when the view narrows, because a
    // panel opens on the right, every page slides sideways without any layout
    // signal firing. Anything stuck to a page position has to follow here.
    // Deferred, because the child widgets are still in their old places.
    QTimer::singleShot(0, this, &DocumentView::repositionPageOverlays);
}

void DocumentView::repositionPageOverlays()
{
    m_selection->relayout();
    if (m_find) m_find->relayout();
    m_imageLayer->relayout();
    m_linkLayer->relayout();
    m_noteLayer->relayout();
    m_drawingLayer->relayout();
    for (PageOverlay *overlay : std::as_const(m_overlays))
        overlay->relayout();
    repositionEditorFrame();
}

void DocumentView::scrollToSearchMatch(int page, const QRectF &bounds)
{
    if (page < 0 || page >= pageLabelCount()) return;
    if (m_viewMode == ViewMode::Grid) setViewMode(ViewMode::Single);

    QMetaObject::invokeMethod(this, [this, page, bounds]() {
        if (m_layout) m_layout->activate();
        const QLabel *label = pageLabel(page);
        if (!label) return;
        const int matchY = label->pos().y()
            + qRound(bounds.center().y() * screenScale());
        verticalScrollBar()->setValue(
            qMax(0, matchY - viewport()->height() / 2));
        reportCurrentPage();
        m_find->relayout();
    }, Qt::QueuedConnection);
}

QRect DocumentView::visibleCanvasRect() const
{
    // The canvas is a child of the viewport and scrolling moves it, so its
    // negated position is the viewport origin in canvas coordinates. Reading
    // it from the widget instead of the scrollbars also covers the case where
    // the canvas is smaller than the viewport and gets centred.
    return QRect(-m_canvas->pos(), viewport()->size());
}

void DocumentView::syncVisibleRect()
{
    if (m_viewMode != ViewMode::Single) return;
    m_layoutEngine->setVisibleRect(visibleCanvasRect());
}

// ── Page rendering (delegated to PageLayoutEngine) ────────────────────────────

QLabel *DocumentView::pageLabel(int page) const
{
    return m_layoutEngine->pageLabel(page);
}

int DocumentView::pageLabelCount() const
{
    return m_layoutEngine->pageLabelCount();
}

void DocumentView::rerenderPage(int page)
{
    m_layoutEngine->rerenderPage(page);
}

void DocumentView::setViewMode(ViewMode mode)
{
    if (m_viewMode == mode) return;
    m_selection->clear();   // grid view has no page-text geometry to anchor to
    m_viewMode = mode;

    if (mode == ViewMode::Grid) {
        if (m_src->pageCount() == 0) { m_viewMode = ViewMode::Single; return; }
        m_layoutEngine->buildGridItems();
        m_canvas->hide();
        takeWidget();
        setWidget(m_gridCanvas);
        m_gridCanvas->show();
        // Defer: viewport()->width() is reliable after the scroll area processes
        // the new widget.
        QMetaObject::invokeMethod(this, [this]() {
            m_layoutEngine->relayoutGrid(viewport()->width());
        }, Qt::QueuedConnection);
    } else {
        m_layoutEngine->clearGrid();
        m_gridCanvas->hide();
        m_gridCanvas->setMinimumHeight(0);
        takeWidget();
        setWidget(m_canvas);
        m_canvas->show();
        // Same as above: the canvas position is only meaningful after the
        // scroll area has taken the widget back.
        QMetaObject::invokeMethod(this, [this]() { syncVisibleRect(); },
                                  Qt::QueuedConnection);
    }
    Q_EMIT viewModeChanged(mode);
}

// ── Context menu (editor / page selection) ────────────────────────────────────

void DocumentView::showGeneralContextMenu(const QPoint &globalPos)
{
    auto *focusEdit    = qobject_cast<QTextEdit *>(QApplication::focusWidget());
    const bool hasEdit = focusEdit && m_editorFrame && m_editorFrame->isVisible();
    const bool hasSel  = hasEdit && focusEdit->textCursor().hasSelection();
    // Text marked on the page with the select tool — copy only, the page text
    // itself is not modified from here.
    const bool hasPageSel = !hasEdit && !selectedText().isEmpty();

    QMenu menu(this);
    QAction *copy = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")),
                                   tr("Kopieren"));
    copy->setEnabled(hasSel || hasPageSel);

    QAction *act = menu.exec(globalPos);
    if (act != copy) return;

    if (hasPageSel)      copySelectedText();
    else if (focusEdit)  focusEdit->copy();
}

// ── Edit mode ─────────────────────────────────────────────────────────────────

qreal DocumentView::screenScale() const
{
#ifdef HAVE_PDF_RENDERING
    return PdfRenderer::screenScale(m_zoomCtl->zoom());
#else
    return m_zoomCtl->zoom() / 100.0;
#endif
}

std::pair<int, QLabel *> DocumentView::pageAtCanvasPos(const QPoint &canvasPos) const
{
    for (int i = 0; i < pageLabelCount(); ++i)
        if (pageLabel(i)->geometry().contains(canvasPos))
            return { i, pageLabel(i) };
    return { -1, nullptr };
}

bool DocumentView::eventFilter(QObject *obj, QEvent *e)
{
    // Page labels have WA_TransparentForMouseEvents so all clicks fall through to
    // m_canvas (their parent). We also handle viewport() for clicks in the margins.
    const bool fromCanvas   = (obj == m_canvas);
    const bool fromViewport = (obj == viewport());
    if (!fromCanvas && !fromViewport)
        return QScrollArea::eventFilter(obj, e);

    // Helpers: m_canvas coords are the canonical space; rubber band lives in viewport.
    const QPoint scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
    auto toCanvas   = [&](const QPoint &p) { return fromCanvas ? p : p + scroll; };
    auto toViewport = [&](const QPoint &p) { return fromCanvas ? p - scroll : p; };

    if (e->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(e);
        if (me->button() == Qt::LeftButton) {
            const QPoint cvsPos = toCanvas(me->pos());
            const QPoint vpPos  = toViewport(me->pos());

            if (m_editMode && m_tool == Tool::Text) {
#ifdef HAVE_PDF_RENDERING
                // Let TextBoxFrame/InlineEditor handle clicks inside the active frame.
                if (m_editorFrame->isVisible() && m_editorFrame->geometry().contains(cvsPos))
                    return QScrollArea::eventFilter(obj, e);
                // Commit the active edit before starting a new one.
                // cancelCurrentEdit must NOT be used here — the user's typed
                // text must be kept, not discarded.
                commitCurrentEdit(m_editorFrame->currentText());
#endif
                m_textDragStart = cvsPos;   // stored in canvas coords
                m_textTracking  = true;
                m_textDragging  = false;
                return true;
            }

            if (m_tool == Tool::Image) {
                // Click on a detected image frame extracts and places it;
                // otherwise start the rubber band, but only on a page.
                if (m_imageLayer->takeDetectedRegionAt(cvsPos)) return true;
                m_imageLayer->handlePress(cvsPos);   // false → outside page, swallowed
                return true;
            }

            if (m_tool == Tool::Comment) {
                m_noteLayer->addAt(cvsPos);
                return true;
            }

            if (m_tool == Tool::Draw) {
                m_drawingLayer->handlePress(cvsPos);
                return true;
            }

            switch (m_tool) {
            case Tool::Select:
            case Tool::Attach:
                m_selection->handlePress(cvsPos);
                break;
            case Tool::Pan:
                m_panStart        = vpPos;
                m_panScrollOrigin = { horizontalScrollBar()->value(),
                                     verticalScrollBar()->value() };
                viewport()->setCursor(Qt::ClosedHandCursor);
                break;
            default: break;
            }
        }
    } else if (e->type() == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(e);
        if (m_tool == Tool::Draw && (me->buttons() & Qt::LeftButton)) {
            m_drawingLayer->handleMove(toCanvas(me->pos()));
            return true;
        }
        // Hover feedback over detected content regions (Acrobat-style).
        if (m_editMode && m_tool == Tool::Text && !m_textTracking)
            m_hover->showAt(toCanvas(me->pos()));
        else
            m_hover->hide();
        if (m_editMode && m_tool == Tool::Text && m_textTracking) {
            const QPoint cvsPos = toCanvas(me->pos());
            if (!m_textDragging && (cvsPos - m_textDragStart).manhattanLength() > 6)
                m_textDragging = true;
            if (m_textDragging) {
                m_rubberBand->setGeometry(
                    QRect(toViewport(m_textDragStart), toViewport(cvsPos)).normalized());
                m_rubberBand->show();
            }
            return true;
        }
        if (m_tool == Tool::Image && m_imageLayer->isDragTracking()) {
            bool dragging = false;
            const QPoint cvsPos = m_imageLayer->handleMove(toCanvas(me->pos()), &dragging);
            if (dragging) {
                m_rubberBand->setGeometry(
                    QRect(toViewport(m_imageLayer->dragStart()),
                          toViewport(cvsPos)).normalized());
                m_rubberBand->show();
            }
            return true;
        }
        // Select/Pan are navigation tools — they work in edit mode as well,
        // matching the press handler above which is not gated on m_editMode.
        {
            const QPoint vpPos = toViewport(me->pos());
            switch (m_tool) {
            case Tool::Select:
            case Tool::Attach:
                if ((me->buttons() & Qt::LeftButton)
                        && m_selection->handleMove(toCanvas(me->pos())))
                    return true;
                break;
            case Tool::Pan:
                if (me->buttons() & Qt::LeftButton) {
                    const QPoint d = vpPos - m_panStart;
                    horizontalScrollBar()->setValue(m_panScrollOrigin.x() - d.x());
                    verticalScrollBar()->setValue(m_panScrollOrigin.y() - d.y());
                    return true;
                }
                break;
            default: break;
            }
        }
    } else if (e->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(e);
        if (me->button() == Qt::LeftButton) {
            if (m_tool == Tool::Draw) {
                m_drawingLayer->handleRelease();
                return true;
            }
            if (m_editMode && m_tool == Tool::Text && m_textTracking) {
                m_textTracking = false;
                if (m_textDragging) {
                    m_textDragging = false;
                    const QRect band = m_rubberBand->geometry();   // viewport coords
                    m_rubberBand->hide();
                    if (band.width() > 30 && band.height() > 15)
                        createTextFrame(band);                       // expects viewport rect ✓
                } else {
                    handleEditClick(m_textDragStart);                // canvas coords ✓
                }
                return true;
            }
            if (m_tool == Tool::Image && m_imageLayer->isDragTracking()) {
                if (m_imageLayer->handleRelease()) {
                    const QRect band = m_rubberBand->geometry();  // viewport coords
                    m_rubberBand->hide();
                    if (band.width() > 20 && band.height() > 20) {
                        const QString path = QFileDialog::getOpenFileName(this,
                            tr("Bild einfügen"), {},
                            tr("Bilder (*.png *.jpg *.jpeg *.bmp *.gif *.tiff *.webp);;Alle Dateien (*)"));
                        if (!path.isEmpty()) {
                            const QImage img(path);
                            if (!img.isNull())
                                m_imageLayer->placeInRect(img, band.translated(scroll));
                        }
                    }
                }
                return true;
            }
            switch (m_tool) {
            case Tool::Select:
            case Tool::Attach:
                m_rubberBand->hide();
                if (m_selection->handleRelease()) return true;
                break;
            case Tool::Pan:    viewport()->setCursor(Qt::OpenHandCursor);   break;
            default: break;
            }
        }
    } else if (e->type() == QEvent::ContextMenu) {
        if (m_tool == Tool::Attach) {
            auto *ce = static_cast<QContextMenuEvent *>(e);
            if (m_linkLayer->showEmptyContextMenu(
                    toCanvas(ce->pos()), ce->globalPos(), m_selection->selectedParts()))
                m_selection->clear();
            return true;
        }
        // Show the general context menu whenever edit mode is active or a
        // content-editing tool is selected.  For the image tool, ImageAnnotation
        // accepts its own context-menu event so it never reaches the canvas — the
        // general menu appears only when clicking on empty canvas area.
        // Marked page text offers "Kopieren" regardless of the mode.
        if (m_editMode || m_tool == Tool::Text || m_tool == Tool::Image
                || m_selection->hasSelection()) {
            auto *ce = static_cast<QContextMenuEvent *>(e);
            showGeneralContextMenu(ce->globalPos());
            return true;
        }
    }

    return QScrollArea::eventFilter(obj, e);
}

// ── Drag & Drop ───────────────────────────────────────────────────────────────

static bool isImagePath(const QString &p)
{
    static const QStringList exts = {
        ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tiff", ".tif", ".webp"
    };
    for (const QString &ext : exts)
        if (p.endsWith(ext, Qt::CaseInsensitive)) return true;
    return false;
}

void DocumentView::dragEnterEvent(QDragEnterEvent *e)
{
    if (!e->mimeData()->hasUrls()) { e->ignore(); return; }
    for (const QUrl &url : e->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive) ||
            (m_tool == Tool::Image && isImagePath(path))) {
            e->acceptProposedAction();
            return;
        }
        // Ask the overlays here too and not only on drop: what is turned away
        // now never arrives, the cursor shows a no-entry sign and dropEvent()
        // is never called.
        for (const PageOverlay *overlay : std::as_const(m_overlays)) {
            if (!overlay->acceptsDroppedFile(path)) continue;
            e->acceptProposedAction();
            return;
        }
    }
    e->ignore();
}

void DocumentView::dragMoveEvent(QDragMoveEvent *e) { e->acceptProposedAction(); }

void DocumentView::dropEvent(QDropEvent *e)
{
    for (const QUrl &url : e->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
            // Opening a document is not the view's call — it decides which tab
            // it lands in and has to be recorded as the last opened file.
            Q_EMIT pdfDropped(path);
            e->acceptProposedAction();
            return;
        }
        // Overlays first: they know file kinds the Core does not. One that
        // says yes has taken the file.
        if (!m_overlays.isEmpty()) {
            const QPoint scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
            const auto [page, label] = pageAtCanvasPos(e->position().toPoint() + scroll);
            bool taken = false;
            QString replacement;
            for (PageOverlay *overlay : std::as_const(m_overlays))
                if (overlay->handleDroppedFile(path, page, &replacement)) { taken = true; break; }
            if (taken) {
                e->acceptProposedAction();
                if (!replacement.isEmpty())
                    openWorkingCopy(replacement, currentFile(),
                                    { DocumentHistory::Kind::PageAdded,
                                      qMax(0, page + 1) });
                return;
            }
        }

        if (m_tool == Tool::Image && isImagePath(path)) {
            const QImage img(path);
            if (!img.isNull()) {
                const QPoint vpPos  = e->position().toPoint();
                const QPoint scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
                m_imageLayer->place(img, vpPos + scroll);
                e->acceptProposedAction();
                return;
            }
        }
    }
}
