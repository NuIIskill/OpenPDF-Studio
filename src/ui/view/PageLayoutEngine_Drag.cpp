#include "ui/view/PageLayoutEngine.hpp"
#include "ui/view/PageView.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentWorker.hpp"
#  include "engine/edit/EditSession.hpp"
#  include "engine/render/PdfRenderer.hpp"
#endif

#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#ifdef HAVE_PDF_RENDERING
namespace {

// The offset that turns one list of edits into the other, when the only
// difference is that the edits holding text moved by the same amount.
bool movedBy(const QList<EditSession::Edit> &from, const QList<EditSession::Edit> &to, QPointF *delta)
{
    if (from.size() != to.size()) return false;
    bool moved = false;
    QPointF by;
    for (int i = 0; i < from.size(); ++i) {
        const EditSession::Edit &a = from.at(i), &b = to.at(i);
        if (a == b) continue;
        if (a.newText.isNull() || std::abs(a.pdfBounds.width() - b.pdfBounds.width()) > 1e-6
                || std::abs(a.pdfBounds.height() - b.pdfBounds.height()) > 1e-6)
            return false;
        const QPointF d = b.pdfBounds.topLeft() - a.pdfBounds.topLeft();
        if (moved && (d - by).manhattanLength() > 1e-6) return false;
        // The box carries its place too, and moves with it.
        EditSession::Edit same = a;
        same.pdfBounds = b.pdfBounds;
        same.box.bounds = a.box.bounds.translated(d);
        if (std::abs(same.box.bounds.left() - b.box.bounds.left()) < 1e-6
                && std::abs(same.box.bounds.top() - b.box.bounds.top()) < 1e-6)
            same.box.bounds = b.box.bounds;
        if (same != b) return false;
        by = d;
        moved = true;
    }
    if (moved) *delta = by;
    return moved;
}

}

void PageLayoutEngine::setDraggingText(bool on)
{
    if (on == m_draggingText) return;
    m_draggingText = on;
    if (on) {
        // Prepared while the box sat still, or asked for as the button goes down.
        if (m_drag.page >= 0 && (m_drag.page != m_preview.page || m_drag.start != m_preview.edits
                                 || m_drag.zoom != m_zoom))
            clearDrag();
        if (m_drag.page < 0) beginDrag(m_preview.page, {});
        if (m_drag.page >= 0) {
            m_dragTimer->start();
            stripDragText();
        }
    }
    if (!on && m_drag.page >= 0 && !m_drag.ending) finishDrag();
}

void PageLayoutEngine::prepareDrag()
{
    // Only a page too slow to patch on every move needs its pictures ahead.
    const int page = m_preview.page;
    if (m_draggingText || m_drag.page >= 0 || page < 0
            || !(tiled(page) || m_slowToPatch.contains(page) || m_wholeRenderMs.value(page) >= kSyncRenderMs))
        return;
    beginDrag(page, {});
    if (m_dragTimer) m_dragTimer->stop();
}

void PageLayoutEngine::followDrag(int page, QList<EditSession::Edit> &edits)
{
    QPointF delta;
    if (m_drag.page >= 0 && !m_drag.ending && !m_draggingText) {
        if (page != m_drag.page || edits != m_drag.start) clearDrag();
        return;
    }
    if (m_drag.page >= 0 && !m_drag.ending) {
        if (m_draggingText && page == m_drag.page && m_zoom == m_drag.zoom
                && movedBy(m_drag.start, edits, &delta)) {
            m_drag.delta = delta;
            // Until the picture is laid the page holds the text where it was:
            // the part rendered without it must fit what is shown.
            edits = m_drag.stripped ? m_drag.without : m_drag.start;
            if (m_drag.stripped) placeDragPicture();
            return;
        }
        finishDrag();
        return;
    }
    if (m_draggingText && m_drag.page < 0 && page == m_preview.page
            && movedBy(m_preview.edits, edits, &delta))
        beginDrag(page, delta);
}

void PageLayoutEngine::beginDrag(int page, const QPointF &delta)
{
    if (!m_worker || page < 0 || page != m_preview.page) return;
    bool hasText = false;
    for (const EditSession::Edit &e : std::as_const(m_preview.edits)) hasText |= !e.newText.isNull();
    if (!hasText) return;
    m_drag = {};
    m_drag.page  = page;
    m_drag.zoom  = m_zoom;
    m_drag.id    = ++m_versionCounter;
    m_drag.start = m_preview.edits;
    m_drag.delta = delta;
    for (const EditSession::Edit &e : std::as_const(m_preview.edits))
        if (e.newText.isNull()) m_drag.without.append(e);
    QRectF text;
    double size = 12.0;
    for (const EditSession::Edit &e : std::as_const(m_preview.edits)) {
        if (e.newText.isNull()) continue;
        text = text.isNull() ? e.pdfBounds : text.united(e.pdfBounds);
        size = qMax(size, e.fontSizePt);
    }
    const qreal scale = deviceScale();
    const QRectF around = text.adjusted(-size * 2.0, -size * 2.0, size * 2.0, size * 2.0);
    m_drag.pixels = QRectF(around.topLeft() * scale, around.size() * scale).toAlignedRect()
                        .intersected(QRect(QPoint(), fullPixels(page)));
    if (m_drag.pixels.isEmpty()) { m_drag = {}; return; }
    if (!m_dragTimer) {
        m_dragTimer = new QTimer(this);
        m_dragTimer->setInterval(16);
        connect(m_dragTimer, &QTimer::timeout, this, &PageLayoutEngine::checkDrag);
    }
    m_dragTimer->start();
    m_drag.since.start();
    scheduleRender(0);
}

QList<DocumentWorker::Render> PageLayoutEngine::askDragRenders() const
{
    if (m_drag.page < 0 || m_drag.stripped || m_drag.ending) return {};
    QList<DocumentWorker::Render> renders;
    // The text alone, and the page under it without the text.
    if (m_drag.image.isNull()) {
        DocumentWorker::Render render;
        render.page    = m_drag.page;
        render.scale   = deviceScale();
        render.zoom    = kDragTextTag + m_drag.zoom;
        render.version = m_drag.id;
        render.area    = m_drag.pixels;
        auto session = std::make_shared<EditSession>();
        for (const EditSession::Edit &e : m_drag.start)
            if (!e.newText.isNull()) session->addEdit(e);
        render.session  = std::move(session);
        render.finish   = true;
        render.textOnly = true;
        renders.append(render);
    }
    if (m_drag.base.isNull()) {
        DocumentWorker::Render render;
        render.page    = m_drag.page;
        render.scale   = deviceScale();
        render.zoom    = kDragBaseTag + m_drag.zoom;
        render.version = m_drag.id;
        render.area    = m_drag.pixels;
        render.session = std::make_shared<EditSession>(stateWith(m_drag.page, m_drag.without));
        render.finish  = true;
        renders.append(render);
    }
    return renders;
}

void PageLayoutEngine::takeDragRender(int zoom, int version, const QImage &image)
{
    if (m_drag.page < 0 || m_drag.ending || m_drag.stripped || version != m_drag.id
            || image.isNull() || m_zoom != m_drag.zoom)
        return;
    if (zoom == kDragTextTag + m_drag.zoom) m_drag.image = image;
    else if (zoom == kDragBaseTag + m_drag.zoom) m_drag.base = image;
    if (m_draggingText) stripDragText();
}

void PageLayoutEngine::stripDragText()
{
    if (m_drag.image.isNull() || m_drag.base.isNull() || m_drag.stripped || m_drag.ending) return;
    const int page = m_drag.page;
    QLabel *lbl = m_pageLabels.value(page, nullptr);
    const auto it = m_rendered.find(page);
    // The part under the text replaces the text only where the page shows
    // the state it was rendered from.
    if (!lbl || it == m_rendered.end() || it->zoom != m_zoom || m_preview.page != page
            || m_preview.edits != m_drag.start || !shownMatchesState(page))
        return;
    const bool isTiled = tiled(page);
    if (isTiled && !m_tiles.current(page, m_zoom, it->version)) return;
    if (!isTiled && (it->pixmap.size() != fullPixels(page)
                     || !qFuzzyCompare(it->pixmap.devicePixelRatio(), m_canvas->devicePixelRatioF())))
        return;
    const QRect changed = changedAt(page, deviceScale()).united(m_drag.pixels);
    m_preview.edits = m_drag.without;
    m_drag.stripped = true;
    const int next = bumpVersion(page);
    QPixmap shown = it->pixmap;
    if (isTiled) {
        m_tiles.patch(page, m_drag.pixels, m_drag.base, next);
        Stale &stale = m_staleBase[page];
        stale.area = stale.area.united(m_drag.pixels);
        m_staleBaseTimer->start();
    } else {
        patchPixmap(shown, m_drag.pixels, m_drag.base);
        PageView::setPicture(lbl, shown);
    }
    m_rendered.insert(page, { shown, m_zoom, next });
    noteChanged(page, next, changed, deviceScale());
    m_inexact.insert(page);
    m_settleTimer->start();
    placeDragPicture();
    if (m_drag.picture) m_drag.picture->show();
}

void PageLayoutEngine::placeDragPicture()
{
    QLabel *lbl = m_pageLabels.value(m_drag.page, nullptr);
    if (!lbl || m_drag.image.isNull()) return;
    const qreal dpr = m_canvas->devicePixelRatioF();
    if (!m_drag.picture || m_drag.picture->parentWidget() != lbl) {
        delete m_drag.picture;
        m_drag.picture = new QLabel(lbl);
        m_drag.picture->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_drag.picture->setAttribute(Qt::WA_TranslucentBackground);
        QPixmap pixmap = QPixmap::fromImage(m_drag.image);
        pixmap.setDevicePixelRatio(dpr);
        m_drag.picture->setPixmap(pixmap);
        m_drag.picture->resize(pixmap.deviceIndependentSize().toSize());
        m_drag.picture->hide();
    }
    const QPointF at = QPointF(m_drag.pixels.topLeft()) / dpr
                     + m_drag.delta * PdfRenderer::screenScale(m_zoom);
    // On whole device pixels, as the page renders it.
    m_drag.picture->move((at * dpr).toPoint() / dpr);
    m_drag.picture->raise();
}

bool PageLayoutEngine::shownMatchesState(int page) const
{
    const int version = m_pageVersions.value(page);
    return !m_refreshQueue.contains(page) && showsVersion(page, version)
        && renderedState(page, version).snapshotEdits() == stateFor(page).snapshotEdits();
}

void PageLayoutEngine::checkDrag()
{
    if (m_drag.page < 0) { if (m_dragTimer) m_dragTimer->stop(); return; }
    if (!m_drag.stripped) {
        if (m_drag.ending) clearDrag();
        else stripDragText();
        return;
    }
    // Tiles that arrive later are laid on top; the picture stays above them.
    if (m_drag.picture && m_drag.picture->parentWidget()
            && m_drag.picture->parentWidget()->children().constLast() != m_drag.picture)
        m_drag.picture->raise();
    const bool there = shownMatchesState(m_drag.page);
    if (!m_drag.ending) {
        return;
    }
    // The picture stays until the page shows the text where it was dropped.
    constexpr int kLongestWaitMs = 3000;
    if (there || m_drag.since.elapsed() > kLongestWaitMs) clearDrag();
}

void PageLayoutEngine::finishDrag()
{
    m_drag.ending = true;
    m_drag.since.start();
    giveTextBack();
    if (!m_drag.stripped || !m_drag.picture || m_drag.picture->isHidden()) clearDrag();
}

void PageLayoutEngine::giveTextBack()
{
    // The page shows the text again, where the picture last was.
    if (!m_drag.stripped || m_preview.page != m_drag.page || m_preview.edits != m_drag.without) return;
    QList<EditSession::Edit> moved = m_drag.start;
    for (EditSession::Edit &e : moved)
        if (!e.newText.isNull()) {
            e.pdfBounds.translate(m_drag.delta);
            e.box.bounds.translate(m_drag.delta);
        }
    m_preview.edits = moved;
    refreshSoon(m_drag.page);
}

void PageLayoutEngine::clearDrag()
{
    giveTextBack();
    delete m_drag.picture;
    m_drag = {};
    if (m_dragTimer) m_dragTimer->stop();
}
#endif
