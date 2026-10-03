#include "ui/view/PageLayoutEngine.hpp"
#include "ui/view/PageView.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentWorker.hpp"
#  include "engine/document/PdfBackend.hpp"
#  include "engine/edit/EditSession.hpp"
#  include "engine/render/PdfRenderer.hpp"
#endif

#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QWidget>

#ifdef HAVE_PDF_RENDERING
EditSession PageLayoutEngine::stateFor(int page) const
{
    EditSession state = m_session ? *m_session : EditSession();
    if (m_blank.page == page) {
        EditSession::Edit blank;
        blank.page       = page;
        blank.pdfBounds  = m_blank.bounds;
        blank.eraseRects = m_blank.rects;
        state.addEdit(std::move(blank));
    }
    if (m_preview.page == page)
        for (const EditSession::Edit &e : m_preview.edits)
            state.addEdit(e);
    return state;
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
    if (patchPage(page)) return;
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
                                             const QList<QRectF> &eraseRects)
{
    if (page < 0 || page >= m_pageLabels.size()) return;
    // Zoom levels being prepared would hold the worker while text is edited.
    if (m_blank.page < 0 && m_preview.page < 0) scheduleRender(0);
    m_blank = { page, pdfBoundsPts, eraseRects };
    refreshSoon(page);
}

void PageLayoutEngine::setPreviewEdits(int page, const QList<EditSession::Edit> &edits)
{
    if (m_preview.page == page && m_preview.edits == edits) return;
    const int was = m_preview.page;
    if (was < 0 && m_blank.page < 0 && page >= 0) scheduleRender(0);
    m_preview = { page, edits };
    if (was >= 0 && was != page) refreshSoon(was);
    if (page >= 0) refreshSoon(page);
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
}
