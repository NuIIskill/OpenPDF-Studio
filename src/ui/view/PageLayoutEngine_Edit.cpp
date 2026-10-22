#include "ui/view/PageLayoutEngine.hpp"
#include "ui/view/PageView.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentWorker.hpp"
#  include "engine/document/PdfBackend.hpp"
#  include "engine/edit/EditSession.hpp"
#  include "engine/render/PdfRenderer.hpp"
#endif

#include <QElapsedTimer>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QWidget>

#ifdef HAVE_PDF_RENDERING
EditSession PageLayoutEngine::stateFor(int page, bool withPreview) const
{
    return stateWith(page, withPreview && m_preview.page == page ? m_preview.edits
                                                                  : QList<EditSession::Edit>());
}

EditSession PageLayoutEngine::stateWith(int page, const QList<EditSession::Edit> &preview) const
{
    EditSession state = m_session ? *m_session : EditSession();
    // A preview that brings its own blank, a framed edit going back into its
    // paragraph, stands for what closing gives; the editor's blank is not part of it.
    bool ownBlank = false;
    for (const EditSession::Edit &e : preview) ownBlank |= e.newText.isNull();
    if (m_blank.page == page && !ownBlank) {
        EditSession::Edit blank;
        blank.page       = page;
        blank.pdfBounds  = m_blank.bounds;
        blank.eraseRects = m_blank.rects;
        blank.scanned    = m_blank.paper.isValid();
        blank.bgColor    = m_blank.paper;
        state.addEdit(std::move(blank));
    }
    for (const EditSession::Edit &e : preview)
        state.addEdit(e);
    return state;
}

bool PageLayoutEngine::layersText(int page) const
{
    // A page too slow to render on every key shows the edit's text alone laid
    // over it, rendered once without it; whole pages only, not tiles.
    return m_worker && m_renderer && m_preview.page == page && !m_preview.edits.isEmpty()
        && !tiled(page) && !(m_drag.page == page && m_drag.stripped)
        && (m_slowToPatch.contains(page) || m_wholeRenderMs.value(page) >= kSlowPatchMs * 2);
}

bool PageLayoutEngine::textBaseReady(int page) const
{
    return m_textBase.page == page && m_textBase.zoom == m_zoom && !m_textBase.image.isNull()
        && m_textBase.image.size() == fullPixels(page)
        && m_textBase.state == stateFor(page, false).snapshotEdits();
}

DocumentWorker::Render PageLayoutEngine::askTextBase(int page)
{
    const QList<EditSession::Edit> state = stateFor(page, false).snapshotEdits();
    // A number of its own: the worker keeps a page loaded per number, and one
    // loaded without the edit's text must not serve a render of the page with it.
    if (m_textAsked.page != page || m_textAsked.zoom != m_zoom || m_textAsked.state != state)
        m_textAsked = { page, m_zoom, ++m_versionCounter, state, {}, {}, -1 };
    DocumentWorker::Render render;
    render.page    = page;
    render.scale   = deviceScale();
    render.zoom    = kTextBaseTag + m_zoom;
    render.version = m_textAsked.version;
    render.session = std::make_shared<EditSession>(stateFor(page, false));
    render.finish  = true;
    return render;
}

DocumentWorker::Render PageLayoutEngine::askTextLayer(int page)
{
    const qreal scale = deviceScale();
    QRectF text;
    double size = 12.0;
    auto session = std::make_shared<EditSession>();
    for (const EditSession::Edit &e : m_preview.edits) {
        text = text.isNull() ? e.pdfBounds : text.united(e.pdfBounds);
        size = qMax(size, e.fontSizePt);
        session->addEdit(e);
    }
    // Room for text running past its box, as at the bottom of the page.
    const QRectF around = text.adjusted(-size * 2.0, -size * 2.0, size * 2.0, size * 2.0);
    DocumentWorker::Render render;
    render.page     = page;
    render.scale    = scale;
    render.zoom     = kTextLayerTag + m_zoom;
    render.version  = m_pageVersions.value(page);
    render.area     = QRectF(around.topLeft() * scale, around.size() * scale).toAlignedRect()
                          .intersected(QRect(QPoint(), m_textBase.image.size()));
    render.session  = std::move(session);
    render.finish   = true;
    render.textOnly = true;
    return render;
}

void PageLayoutEngine::layText(int page, const QRect &pixels, const QImage &layer, int version)
{
    QLabel *lbl = m_pageLabels.value(page, nullptr);
    if (!lbl) return;
    const QImage &base = m_textBase.image;
    // Where the picture is still the last one laid, only what the text
    // covered then and now is painted again; else the whole page.
    const auto it = m_rendered.constFind(page);
    const bool laid = it != m_rendered.cend() && it->version == m_textBase.shown
                   && it->zoom == m_zoom && it->pixmap.size() == base.size();
    QPixmap shown;
    QRect patch = pixels;
    if (laid) {
        shown = it->pixmap;
        patch = pixels.united(m_textBase.drawn);
    } else {
        shown = QPixmap::fromImage(base);
        shown.setDevicePixelRatio(m_canvas->devicePixelRatioF());
    }
    QImage merged = base.copy(patch);
    {
        QPainter painter(&merged);
        painter.drawImage(pixels.topLeft() - patch.topLeft(), layer);
    }
    patchPixmap(shown, patch, merged);
    PageView::setPicture(lbl, shown);
    m_textBase.drawn = pixels;
    m_textBase.shown = version;
    m_rendered.insert(page, { shown, m_zoom, version });
    m_staleBase.remove(page);
}

std::shared_ptr<const EditSession> PageLayoutEngine::editRenderState(int page, int version)
{
    // All parts of one version show the same text, also when it changed
    // meanwhile and the change waits for them.
    if (m_editRender.page != page || m_editRender.version != version || !m_editRender.state)
        m_editRender = { page, version, std::make_shared<EditSession>(stateFor(page)), false };
    return m_editRender.state;
}

EditSession PageLayoutEngine::renderedState(int page, int version) const
{
    if (m_editRender.page == page && m_editRender.version == version && m_editRender.state)
        return *m_editRender.state;
    return stateFor(page);
}

bool PageLayoutEngine::showsVersion(int page, int version) const
{
    if (tiled(page))
        return m_tiles.covers(page, wantedTile(page, false), fullPixels(page), m_zoom, version);
    const auto it = m_rendered.constFind(page);
    return it != m_rendered.cend() && it->zoom == m_zoom && it->version == version;
}
#endif

void PageLayoutEngine::noteChanged(int page, int version, const QRect &pixels, qreal scale)
{
    // Also kept in points, so a render at another zoom knows the area too.
    m_changed.insert(page, { version, pixels });
    if (pixels.isEmpty() || scale <= 0) m_changedPts.remove(page);
    else m_changedPts.insert(page, QRectF(QPointF(pixels.topLeft()) / scale, QSizeF(pixels.size()) / scale));
}

QRect PageLayoutEngine::changedAt(int page, qreal scale) const
{
#ifdef HAVE_PDF_RENDERING
    const auto it = m_changedPts.constFind(page);
    if (it == m_changedPts.cend()) return {};
    const QRect area = QRectF(it->topLeft() * scale, it->size() * scale).toAlignedRect()
                           .adjusted(-2, -2, 2, 2);
    return area.intersected(QRect(QPoint(), m_renderer->backend()->pixelSize(page, scale)));
#else
    Q_UNUSED(page) Q_UNUSED(scale)
    return {};
#endif
}

bool PageLayoutEngine::editedOnPage(int page) const
{
#ifdef HAVE_PDF_RENDERING
    if (m_blank.page == page || m_preview.page == page) return true;
    return m_session && (m_session->hasEditsOnPage(page) || m_session->hasImageEditsOnPage(page)
                         || m_session->hasDrawEditsOnPage(page)
                         || m_session->hasLinkEditsOnPage(page));
#else
    Q_UNUSED(page)
    return false;
#endif
}

bool PageLayoutEngine::patchPage(int page)
{
#ifdef HAVE_PDF_RENDERING
    QLabel *lbl = m_pageLabels.value(page, nullptr);
    const auto it = m_rendered.constFind(page);
    const auto known = m_changed.constFind(page);
    const int version = m_pageVersions.value(page);
    if (!lbl || !m_renderer || !m_renderer->backend() || it == m_rendered.cend()
            || known == m_changed.cend() || it->zoom != m_zoom
            || it->version != version || known->version != version)
        return false;
    const bool isTiled = tiled(page);
    if (isTiled && !m_tiles.current(page, m_zoom, version)) return false;
    if (!isTiled && !qFuzzyCompare(it->pixmap.devicePixelRatio(), m_canvas->devicePixelRatioF()))
        return false;

    // Only what differs from the unedited page, now or in the pixmap, is
    // rendered again; everything else on screen is already right.
    if (m_settling) {
        m_settling = false;
        m_settleLeft.clear();
        if (m_worker) m_worker->setRenders({});
        scheduleRender(kScrollRenderDelayMs);
    }
    // A light page, or one where most of it changes, is rendered whole:
    // that is cheaper there and exact.
    constexpr int kWorthPatchingMs = 60;
    const QSize full = fullPixels(page);
    if (!isTiled && m_wholeRenderMs.value(page, kWorthPatchingMs) < kWorthPatchingMs)
        return false;
    if (qint64(known->area.width()) * known->area.height() * 2
            > qint64(full.width()) * full.height())
        return false;

    const EditSession state = stateFor(page);
    const qreal scale = deviceScale();
    // The anchor is kept for the text being edited and what overlaps it; a
    // change somewhere else is patched directly and made exact once editing pauses.
    const auto anchorIt = m_anchors.find(page);
    const bool editingHere = m_blank.page == page || m_preview.page == page;
    const bool anchored = anchorIt != m_anchors.end() && qFuzzyCompare(anchorIt->scale, scale)
                       && (editingHere || anchorIt->area.intersects(known->area));
    const QRect pageRect(QPoint(), full);

    // A part of a page rounds text and decodes images a little differently
    // from the whole page. The last exactly rendered state is rendered over
    // the same part as the new one; where both agree the exact pixels stay,
    // so text around the edit does not shift by a pixel.
    const auto growAnchor = [&](const QRect &need) {
        Anchor &anchor = *anchorIt;
        // Room for the text to grow, mostly to the right and down.
        const int pad = qRound(24 * scale);
        const QRect grown = need.united(anchor.area)
                                .adjusted(-pad / 4, -pad / 4, pad * 2, pad / 2)
                                .intersected(pageRect);
        const PdfBackend::AreaRender old = m_renderer->backend()->renderChanges(
            page, scale, &anchor.state, grown, false);
        if (!old.pixels.contains(grown)) return false;
        QImage exact = old.image.copy(grown.translated(-old.pixels.topLeft()));
        if (isTiled) {
            m_tiles.capture(page, grown, exact);
        } else {
            QPainter painter(&exact);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawImage(QPoint(), base(page, grown));
        }
        if (!anchor.exact.isNull()) {
            QPainter painter(&exact);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawImage(anchor.area.topLeft() - grown.topLeft(), anchor.exact);
        }
        anchor.render = old.image.copy(grown.translated(-old.pixels.topLeft()));
        anchor.exact  = exact;
        anchor.area   = grown;
        return true;
    };
    if (anchored && !known->area.isEmpty() && !anchorIt->area.contains(known->area))
        growAnchor(known->area);

    PdfBackend::AreaRender render = m_renderer->backend()->renderChanges(
        page, scale, &state, anchored ? known->area.united(anchorIt->area) : known->area, false);
    if (render.image.isNull() && !render.pixels.isEmpty()) return false;
    if (anchored && !render.image.isNull() && anchorIt->area != render.pixels
            && growAnchor(render.pixels))
        render = m_renderer->backend()->renderChanges(page, scale, &state, anchorIt->area, false);

    QImage merged = render.image;
    if (anchored && !merged.isNull()) {
        const Anchor &anchor = *anchorIt;
        if (anchor.area == render.pixels) {
            const QPoint at = render.pixels.topLeft() - anchor.area.topLeft();
            for (int y = 0; y < merged.height(); ++y) {
                QRgb *out = reinterpret_cast<QRgb *>(merged.scanLine(y));
                const QRgb *was  = reinterpret_cast<const QRgb *>(anchor.render.constScanLine(at.y() + y)) + at.x();
                const QRgb *keep = reinterpret_cast<const QRgb *>(anchor.exact.constScanLine(at.y() + y)) + at.x();
                for (int x = 0; x < merged.width(); ++x)
                    if (out[x] == was[x]) out[x] = keep[x];
            }
        }
    }

    QPixmap base = it->pixmap;
    const int next = bumpVersion(page);
    if (!merged.isNull()) {
        if (isTiled) {
            m_tiles.patch(page, render.pixels, merged, next);
            // The capped base only shows where the tile does not reach; it
            // catches up once typing pauses or before it could come into view.
            Stale &stale = m_staleBase[page];
            stale.area = stale.area.united(render.pixels);
            m_staleBaseTimer->start();
        } else {
            patchPixmap(base, render.pixels, merged);
            PageView::setPicture(lbl, base);
        }
    } else if (isTiled) {
        m_tiles.patch(page, {}, {}, next);
    }
    m_rendered.insert(page, { base, m_zoom, next });
    noteChanged(page, next, render.changed, scale);
    // Images on a page loaded for a part of it decode at a lower resolution
    // than for the whole page, so the patch can differ slightly from a full
    // render; the worker replaces it once typing pauses.
    m_inexact.insert(page);
    m_settleTimer->start();
    return true;
#else
    Q_UNUSED(page)
    return false;
#endif
}

QImage PageLayoutEngine::base(int page, const QRect &pixels) const
{
    // In device pixels; the pixmap's ratio would make a painter scale it.
    QImage image = m_rendered.value(page).pixmap.copy(pixels).toImage()
                       .convertToFormat(QImage::Format_RGB32);
    image.setDevicePixelRatio(1.0);
    return image;
}

void PageLayoutEngine::catchUpBase(int page)
{
#ifdef HAVE_PDF_RENDERING
    const auto stale = m_staleBase.constFind(page);
    if (stale == m_staleBase.cend()) return;
    const QRect area = stale->area;
    m_staleBase.erase(stale);
    QLabel *lbl = m_pageLabels.value(page, nullptr);
    const auto it = m_rendered.find(page);
    if (!lbl || !m_renderer || it == m_rendered.end() || it->zoom != m_zoom || !tiled(page))
        return;

    const qreal ratio = baseScale(page) / deviceScale();
    const QRect pixels = QRectF(QPointF(area.topLeft()) * ratio, QSizeF(area.size()) * ratio)
                             .toAlignedRect().adjusted(-2, -2, 2, 2);
    const EditSession state = stateFor(page);
    const PdfBackend::AreaRender low = m_renderer->backend()->renderChanges(
        page, baseScale(page), editedOnPage(page) ? &state : nullptr, pixels, false);
    if (low.image.isNull()) return;
    patchPixmap(it->pixmap, low.pixels, low.image);
    PageView::setPicture(lbl, it->pixmap);
    m_inexact.insert(page);
#else
    Q_UNUSED(page)
#endif
}

void PageLayoutEngine::catchUpBases()
{
    const QList<int> pages = m_staleBase.keys();
    for (int page : pages) catchUpBase(page);
}

void PageLayoutEngine::patchPixmap(QPixmap &pixmap, const QRect &pixels, const QImage &image)
{
    // Painted in device pixels, so the patch lands exactly on the render.
    const qreal dpr = pixmap.devicePixelRatio();
    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.setWorldTransform(QTransform::fromScale(1.0 / dpr, 1.0 / dpr));
    painter.drawImage(pixels.topLeft(), image);
}

void PageLayoutEngine::refreshPage(int page)
{
#ifdef HAVE_PDF_RENDERING
    // A page that is slow to render is not patched on the GUI thread: every
    // step of a drag or every key would stall the window. The worker renders
    // the new state and the picture on screen stays until it is there. A
    // newer change waits for a render on its way instead of cancelling it,
    // or typing faster than the page renders would show nothing at all.
    if (layersText(page)) {
        // The worker renders the new text alone; the window never waits for PDFium.
        if (textBaseReady(page)) bumpVersion(page);
        scheduleRender(0);
        return;
    }
    if (m_worker && (m_slowToPatch.contains(page)
                     || (!tiled(page) && m_wholeRenderMs.value(page) >= kSyncRenderMs))) {
        const int version = m_pageVersions.value(page);
        if (m_editRender.page == page && m_editRender.version == version
                && !showsVersion(page, version)) {
            m_editRender.newer = true;
            return;
        }
        bumpVersion(page);
        scheduleRender(0);
        return;
    }
    QElapsedTimer clock;
    clock.start();
    const bool patched = patchPage(page);
    // A patch longer than a frame stutters a drag: the worker takes the page from then on.
    if (patched && clock.elapsed() >= kSyncRenderMs) m_slowToPatch.insert(page);
    if (patched) return;
#else
    if (patchPage(page)) return;
#endif
#ifdef HAVE_PDF_RENDERING
    // Without a render of the page at this zoom yet, as while zooming, it is
    // rendered on the worker like after any zoom step; the stand-in from the
    // zoom before shows until then.
    const auto it = m_rendered.constFind(page);
    if (m_worker && (it == m_rendered.cend() || it->zoom != m_zoom
                     || it->version != m_pageVersions.value(page))) {
        bumpVersion(page);
        scheduleRender(0);
        return;
    }
#endif
    m_rendered.remove(page);
    renderNow(page);
}

void PageLayoutEngine::rerenderPageWithBlank(int page, const QRectF &pdfBoundsPts,
                                             const QList<QRectF> &eraseRects, const QColor &paper)
{
    if (page < 0 || page >= m_pageLabels.size()) return;
    // Zoom levels being prepared would hold the worker while text is edited.
    if (m_blank.page < 0 && m_preview.page < 0) scheduleRender(0);
    m_blank = { page, pdfBoundsPts, eraseRects, paper };
    refreshSoon(page);
}

void PageLayoutEngine::setPreviewEdits(int page, const QList<EditSession::Edit> &given)
{
    QList<EditSession::Edit> edits = given;
#ifdef HAVE_PDF_RENDERING
    if (m_draggingText || m_drag.page >= 0) followDrag(page, edits);
#endif
    if (m_preview.page == page && m_preview.edits == edits) return;
    const int was = m_preview.page;
    if (page != was) m_textBase = m_textAsked = {};
    if (was < 0 && m_blank.page < 0 && page >= 0) scheduleRender(0);
    m_preview = { page, edits };
    if (was >= 0 && was != page) refreshSoon(was);
    if (page >= 0) refreshSoon(page);
#ifdef HAVE_PDF_RENDERING
    if (page >= 0 && !m_draggingText) {
        if (!m_prepareDragTimer) {
            m_prepareDragTimer = new QTimer(this);
            m_prepareDragTimer->setSingleShot(true);
            m_prepareDragTimer->setInterval(300);
            connect(m_prepareDragTimer, &QTimer::timeout, this, &PageLayoutEngine::prepareDrag);
        }
        m_prepareDragTimer->start();
    }
#endif
}

void PageLayoutEngine::refreshSoon(int page)
{
    // Changes of one event round, such as the blank and the first preview
    // of an edit or keys typed quickly, are rendered together.
    m_refreshQueue.insert(page);
    if (!m_refreshTimer->isActive()) m_refreshTimer->start(0);
}

void PageLayoutEngine::refreshQueued()
{
    const QSet<int> pages = std::exchange(m_refreshQueue, {});
    for (int page : pages)
        if (page >= 0 && page < m_pageLabels.size()) refreshPage(page);
#ifdef HAVE_PDF_RENDERING
    if (pages.contains(m_drag.page)) checkDrag();
#endif
}
