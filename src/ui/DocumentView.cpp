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

void DocumentView::repositionForZoom()
{

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

    if (m_edit.activeEditPage < 0 || !m_editorFrame->isVisible()) return;

    const int activePage = m_edit.activeEditPage;
    QTimer::singleShot(0, this, [this, activePage]() {
        if (m_edit.activeEditPage != activePage || !m_editorFrame->isVisible()) return;

        if (m_layout) m_layout->activate();
        const QLabel *lbl = pageLabel(activePage);
        if (!lbl) return;

        const qreal scale = PdfRenderer::screenScale(m_zoomCtl->zoom());
        m_editorFrame->setPageRect(lbl->geometry());
        const QRectF cb(m_edit.activeEditBounds.topLeft() * scale + QPointF(lbl->pos()),
                        m_edit.activeEditBounds.size() * scale);
        m_editorFrame->repositionForZoom(
            cb, qMax(1.0, m_edit.currentEditorRenderSizePt * scale),
            m_edit.currentBox, scale);

        m_edit.refreshLivePreview();
    });
#endif
}

void DocumentView::setTool(Tool tool)
{
    if (tool != Tool::Select) m_selection->clear();
    m_tool = tool;

    m_imageLayer->setToolActive(tool == Tool::Image);
    m_linkLayer->setToolActive(tool == Tool::Attach);
    m_noteLayer->setToolActive(tool == Tool::Comment);
    m_drawingLayer->setActive(m_editMode && tool == Tool::Draw);

    switch (tool) {
    case Tool::Pan:    viewport()->setCursor(Qt::OpenHandCursor);    break;
    case Tool::Text:   viewport()->setCursor(Qt::IBeamCursor);       break;
    case Tool::Select: viewport()->setCursor(Qt::IBeamCursor);       break;
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

int DocumentView::currentPage() const
{
    if (pageLabelCount() == 0) return 0;

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

    if (m_viewMode == ViewMode::Grid)
        setViewMode(ViewMode::Single);

    if (m_layout) m_layout->activate();

    constexpr int kTopGap = 20;
    const int target = qMax(0, pageLabel(page)->pos().y() - kTopGap);
    verticalScrollBar()->setValue(target);

    if (allowRetry && verticalScrollBar()->value() != target) {

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

    return QRect(-m_canvas->pos(), viewport()->size());
}

void DocumentView::syncVisibleRect()
{
    if (m_viewMode != ViewMode::Single) return;
    m_layoutEngine->setVisibleRect(visibleCanvasRect());
}

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
    m_selection->clear();
    m_viewMode = mode;

    if (mode == ViewMode::Grid) {
        if (m_src->pageCount() == 0) { m_viewMode = ViewMode::Single; return; }
        m_layoutEngine->buildGridItems();
        m_canvas->hide();
        takeWidget();
        setWidget(m_gridCanvas);
        m_gridCanvas->show();

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

        QMetaObject::invokeMethod(this, [this]() { syncVisibleRect(); },
                                  Qt::QueuedConnection);
    }
    Q_EMIT viewModeChanged(mode);
}

void DocumentView::showGeneralContextMenu(const QPoint &globalPos)
{
    auto *focusEdit    = qobject_cast<QTextEdit *>(QApplication::focusWidget());
    const bool hasEdit = focusEdit && m_editorFrame && m_editorFrame->isVisible();
    const bool hasSel  = hasEdit && focusEdit->textCursor().hasSelection();

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

    const bool fromCanvas   = (obj == m_canvas);
    const bool fromViewport = (obj == viewport());
    if (!fromCanvas && !fromViewport)
        return QScrollArea::eventFilter(obj, e);

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

                if (m_editorFrame->isVisible() && m_editorFrame->geometry().contains(cvsPos))
                    return QScrollArea::eventFilter(obj, e);

                commitCurrentEdit(m_editorFrame->currentText());
#endif
                m_textDragStart = cvsPos;
                m_textTracking  = true;
                m_textDragging  = false;
                return true;
            }

            if (m_tool == Tool::Image) {

                if (m_imageLayer->takeDetectedRegionAt(cvsPos)) return true;
                m_imageLayer->handlePress(cvsPos);
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
                    const QRect band = m_rubberBand->geometry();
                    m_rubberBand->hide();
                    if (band.width() > 30 && band.height() > 15)
                        createTextFrame(band);
                } else {
                    handleEditClick(m_textDragStart);
                }
                return true;
            }
            if (m_tool == Tool::Image && m_imageLayer->isDragTracking()) {
                if (m_imageLayer->handleRelease()) {
                    const QRect band = m_rubberBand->geometry();
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

        if (m_editMode || m_tool == Tool::Text || m_tool == Tool::Image
                || m_selection->hasSelection()) {
            auto *ce = static_cast<QContextMenuEvent *>(e);
            showGeneralContextMenu(ce->globalPos());
            return true;
        }
    }

    return QScrollArea::eventFilter(obj, e);
}

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

            Q_EMIT pdfDropped(path);
            e->acceptProposedAction();
            return;
        }

        if (!m_overlays.isEmpty()) {
            const QPoint scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
            const QPoint canvasPosition = e->position().toPoint() + scroll;
            const auto [page, label] = pageAtCanvasPos(canvasPosition);
            bool taken = false;
            QString replacement;
            for (PageOverlay *overlay : std::as_const(m_overlays))
                if (overlay->handleDroppedFile(path, page, canvasPosition,
                                               &replacement)) {
                    taken = true;
                    break;
                }
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
