#include "ui/view/PageLayoutEngine.hpp"

#include "app/SystemMemory.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/PdfBackend.hpp"
#  include "engine/render/PdfRenderer.hpp"
#endif

#include <QCursor>
#include <QLabel>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <QtMath>

#include <algorithm>
#include <cmath>

void PageLayoutEngine::prepareZoom(int percent, const QPoint &canvasAnchor)
{
    if (m_zoomWanted == percent && m_zoomAnchor == canvasAnchor) return;
    m_zoomWanted = percent;
    m_zoomAnchor = canvasAnchor;
    if (percent > 0 && percent != m_zoom) m_zoomDirection = percent > m_zoom ? 1 : -1;
    scheduleRender(0);
}

bool PageLayoutEngine::sharpAt(int percent, const QPoint &canvasAnchor) const
{
#ifdef HAVE_PDF_RENDERING
    if (percent == m_zoom || !m_worker || !m_renderer || !m_canvas->isVisible()) return true;
    const qreal scale = PdfRenderer::screenScale(percent) * m_canvas->devicePixelRatioF();
    const QRect view = zoomView(percent, canvasAnchor);
    for (int page = 0; page < m_pageLabels.size(); ++page) {
        const QLabel *lbl = m_pageLabels.at(page);
        if (lbl->isHidden() || !lbl->geometry().intersects(view)) continue;
        // Pages that show nothing yet or are being edited would only hold the zoom back.
        if (blank(page) || m_blank.page == page || m_preview.page == page
                || m_inexact.contains(page))
            continue;
        const int version = m_pageVersions.value(page);
        const QRect area = wantedArea(page, view, scale);
        if (area.isEmpty()) continue;
        const auto level = m_levels.constFind(levelKey(page, percent));
        if (level != m_levels.cend() && level->version == version && level->pixels.contains(area))
            continue;
        // Tiles kept from an earlier visit of that zoom are as exact.
        if (tiledAt(page, scale)
                && m_tiles.keptCovers(page, percent, version, area,
                                      m_renderer->backend()->pixelSize(page, scale)))
            continue;
        return false;
    }
#else
    Q_UNUSED(percent) Q_UNUSED(canvasAnchor)
#endif
    return true;
}

#ifdef HAVE_PDF_RENDERING
PageLayoutEngine::ZoomRenders PageLayoutEngine::planZoomRenders(
        int first, int last, int shownFirst, int shownLast, bool viewDone, bool idle,
        const std::function<DocumentWorker::Render(int)> &request)
{
    ZoomRenders out;
    // The zoom levels one or more wheel steps away, so a zoom step shows a
    // picture rendered for exactly that zoom. The nearest ones are asked for
    // as soon as the view itself is done, the others only while idle.
    const QList<int> path = zoomPath();
    // Levels stay while memory allows, so a zoom seen before comes back at
    // once; the ones furthest from the view and its zoom go first.
    qint64 levelBytes = 0;
    QList<quint64> spare;
    for (auto it = m_levels.begin(); it != m_levels.end(); ) {
        const int page = int(it.key() >> 16), zoom = int(it.key() & 0xffff);
        if (it->version != m_pageVersions.value(page)) { it = m_levels.erase(it); continue; }
        levelBytes += it->image.sizeInBytes();
        if (!path.contains(zoom) && (page < first || page > last || zoom != m_zoom))
            spare.append(it.key());
        ++it;
    }
    const qint64 levelBudget = SystemMemory::cacheBytes() / 4;
    if (levelBytes > levelBudget) {
        const auto distance = [&](quint64 key) {
            const int page = int(key >> 16), zoom = int(key & 0xffff);
            const int pages = page < first ? first - page : page > last ? page - last : 0;
            return pages * 1000 + qAbs(zoom - m_zoom);
        };
        std::sort(spare.begin(), spare.end(),
                  [&](quint64 a, quint64 b) { return distance(a) > distance(b); });
        for (quint64 key : std::as_const(spare)) {
            if (levelBytes <= levelBudget) break;
            levelBytes -= m_levels.value(key).image.sizeInBytes();
            m_levels.remove(key);
        }
    }
    for (auto it = m_levelAsked.begin(); it != m_levelAsked.end(); ) {
        const int page = int(it.key() >> 16), zoom = int(it.key() & 0xffff);
        if (path.contains(zoom) && it->version == m_pageVersions.value(page)) { ++it; continue; }
        if (page < first || page > last || it->version != m_pageVersions.value(page)
                || qAbs(zoom - m_zoom) > 20 * m_zoomStep)
            it = m_levelAsked.erase(it);
        else ++it;
    }
    // A level is asked for with some room around what it has to cover, so a
    // view that moves by a few pixels between two steps keeps the render running.
    const auto askLevel = [&](int page, int zoom, const QRect &area, const QRect &roomy,
                              QList<DocumentWorker::Render> &into) -> qint64 {
        // A page being edited changes with every key; its levels would only
        // be thrown away.
        if (m_blank.page == page || m_preview.page == page || m_inexact.contains(page))
            return 0;
        const quint64 key = levelKey(page, zoom);
        if (area.isEmpty()) return 0;
        const auto have = m_levels.constFind(key);
        if (have != m_levels.cend() && have->pixels.contains(area)) return 0;
        auto ask = m_levelAsked.find(key);
        if (ask == m_levelAsked.end() || !ask->pixels.contains(area))
            ask = m_levelAsked.insert(key, { m_pageVersions.value(page), roomy.united(area), {} });
        DocumentWorker::Render render = request(page);
        render.zoom    = kLevelTag + zoom;
        render.version = ask->version;
        render.scale   = PdfRenderer::screenScale(zoom) * m_canvas->devicePixelRatioF();
        render.area    = ask->pixels;
        render.finish  = true;
        into.append(render);
        return qint64(area.width()) * area.height();
    };
    const bool wanting = !path.isEmpty();
    // Exactly what each step on the way to a waiting zoom brings into view, in
    // the order the steps are shown.
    for (int zoom : path) {
        if (!m_worker) break;
        const QRect view = zoomView(zoom, m_zoomAnchor);
        const QRect roomy = zoomView(zoom, m_zoomAnchor, 160);
        const qreal scale = PdfRenderer::screenScale(zoom) * m_canvas->devicePixelRatioF();
        for (int page = 0; page < m_pageLabels.size(); ++page)
            if (!m_pageLabels.at(page)->isHidden() && m_pageLabels.at(page)->geometry().intersects(view))
                askLevel(page, zoom, wantedArea(page, view, scale), wantedArea(page, roomy, scale),
                         out.wanted);
    }
    if (m_worker) {
        // One step out and three in the direction the zoom went last are asked
        // for once the view is done; while idle up to twenty steps in and ten out
        // are prepared, nearest first, within about 96 million pixels.
        constexpr qint64 kLevelPixels = 96'000'000;
        const int d = m_zoomDirection;
        QList<int> steps;
        for (int k = 1; k <= 20; ++k) {
            steps.append(k * d);
            if (k <= 10) steps.append(-k * d);
        }
        const QPoint pointer = pointerAnchor();
        // While text is edited the worker stays free for it.
        const bool editing = m_blank.page >= 0 || m_preview.page >= 0 || !m_inexact.isEmpty()
                          || m_settleTimer->isActive();
        qint64 used = 0;
        for (const Level &level : std::as_const(m_levels))
            used += qint64(level.pixels.width()) * level.pixels.height();
        for (int k : std::as_const(steps)) {
            const int zoom = m_zoom + k * m_zoomStep;
            if (zoom < 25 || zoom > 300) continue;
            // While zooming the levels ahead are needed at once; otherwise
            // they wait until nothing else does.
            const bool near = !wanting && !editing && (viewDone || m_levelShown)
                           && (k == d || k == 2 * d || k == 3 * d || k == -d);
            // In edit mode only the nearest, so a click finds the worker free.
            // With one or two cores the window needs them; only the nearest then.
            static const bool fewCores = QThread::idealThreadCount() <= 2;
            if (!near && (!idle || wanting || editing || m_editMode || fewCores)) continue;
            if (!near && used > kLevelPixels) break;
            // One step away a level covers a zoom about any point in view;
            // further away only one about the pointer, which is far smaller.
            const QRect view = zoomView(zoom, pointer, 160);
            const qreal scale = PdfRenderer::screenScale(zoom) * m_canvas->devicePixelRatioF();
            for (int page = shownFirst; page <= shownLast; ++page) {
                const QRect area = qAbs(k) == 1 ? levelArea(page, zoom)
                                                : wantedArea(page, view, scale);
                used += askLevel(page, zoom, area, area, near ? out.levelsNear : out.levelsFar);
            }
        }
    }
    return out;
}

bool PageLayoutEngine::keepZoomRender(int page, int zoom, int version, const QRect &area,
                                      const QImage &image)
{
    if (zoom < kLevelTag) return false;
    const quint64 key = levelKey(page, zoom - kLevelTag);
    const auto ask = m_levelAsked.constFind(key);
    if (ask == m_levelAsked.cend() || ask->version != version || ask->pixels != area)
        return true;
    m_levels.insert(key, { version, area, image });
    m_levelAsked.erase(ask);
    // Arriving while that zoom is already shown, it takes the place of the stand-in.
    if (zoom - kLevelTag == m_zoom && version == m_pageVersions.value(page, -1)
            && !sharp(page))
        adoptLevel(page);
    Q_EMIT zoomPrepared();
    return true;
}

QList<int> PageLayoutEngine::zoomPath() const
{
    // The zoom steps between the shown zoom and the one waiting, the next first.
    QList<int> path;
    for (int zoom = m_zoom; m_zoomWanted > 0 && zoom != m_zoomWanted && path.size() < 4; ) {
        zoom = m_zoomWanted > zoom ? qMin(zoom + m_zoomStep, m_zoomWanted)
                                   : qMax(zoom - m_zoomStep, m_zoomWanted);
        path.append(zoom);
    }
    return path;
}

QRect PageLayoutEngine::wantedArea(int page, const QRect &canvasRect, qreal scale) const
{
    // A page rendered whole at that zoom and mostly in view gets its level
    // whole, so it becomes the page's render.
    const QRect part = pagePixels(page, canvasRect, scale);
    const QSize full = m_renderer ? m_renderer->backend()->pixelSize(page, scale) : QSize();
    if (part.isEmpty() || tiledAt(page, scale)
            || qint64(part.width()) * part.height() * 2 < qint64(full.width()) * full.height())
        return part;
    return QRect(QPoint(), full);
}

QRect PageLayoutEngine::pagePixels(int page, const QRect &canvasRect, qreal scale) const
{
    const QLabel *lbl = m_pageLabels.value(page, nullptr);
    if (!lbl || canvasRect.isEmpty() || !m_renderer) return {};
    const QRect local = canvasRect.intersected(lbl->geometry()).translated(-lbl->pos())
                                  .intersected(lbl->rect());
    if (local.isEmpty()) return {};
    const qreal toPixels = scale / PdfRenderer::screenScale(m_zoom);
    return QRectF(QPointF(local.topLeft()) * toPixels, QSizeF(local.size()) * toPixels)
               .toAlignedRect()
               .intersected(QRect(QPoint(), m_renderer->backend()->pixelSize(page, scale)));
}

QRect PageLayoutEngine::zoomView(int percent, const QPoint &canvasAnchor, int margin) const
{
    // The part of the canvas as it is now that a zoom about the anchor keeps
    // in view, with some room for the gaps between pages, which do not scale.
    const qreal f = qreal(m_zoom) / percent;
    const QPointF a(canvasAnchor);
    QRectF view(a + (QPointF(m_visibleRect.topLeft()) - a) * f, QSizeF(m_visibleRect.size()) * f);
    // The view cannot scroll past the canvas, so near its edges it stops there.
    const QRectF canvas(m_canvas->rect());
    if (view.width() <= canvas.width()) {
        if (view.left() < canvas.left())   view.moveLeft(canvas.left());
        if (view.right() > canvas.right()) view.moveRight(canvas.right());
    }
    if (view.height() <= canvas.height()) {
        if (view.top() < canvas.top())       view.moveTop(canvas.top());
        if (view.bottom() > canvas.bottom()) view.moveBottom(canvas.bottom());
    }
    return view.toAlignedRect().adjusted(-margin, -margin, margin, margin);
}

QPoint PageLayoutEngine::pointerAnchor() const
{
    // Where a wheel zoom would centre now; outside the view, the middle of it.
    const QPoint at = m_canvas->mapFromGlobal(QCursor::pos());
    return m_visibleRect.contains(at) ? at : m_visibleRect.center();
}

QRect PageLayoutEngine::levelArea(int page, int zoom) const
{
    if (m_visibleRect.isEmpty()) return {};
    // Zooming in shows a part of what is in view now; zooming out shows more
    // around it, as much as a zoom about the view's edge would.
    QRect view = m_visibleRect.adjusted(-160, -160, 160, 160);
    if (zoom < m_zoom) {
        const qreal grow = (qreal(m_zoom) / zoom - 1.0) * 1.02;
        const int dx = qCeil(m_visibleRect.width() * grow), dy = qCeil(m_visibleRect.height() * grow);
        view.adjust(-dx, -dy, dx, dy);
    }
    return wantedArea(page, view, PdfRenderer::screenScale(zoom) * m_canvas->devicePixelRatioF());
}

bool PageLayoutEngine::adoptLevel(int page)
{
    // The level becomes the page's render at this zoom, so nothing is rendered
    // again and nothing on screen changes once it is shown.
    const auto it = m_levels.constFind(levelKey(page, m_zoom));
    QLabel *lbl = m_pageLabels.value(page, nullptr);
    const int version = m_pageVersions.value(page);
    if (!lbl || it == m_levels.cend() || it->version != version) return false;
    if (!tiled(page) && it->pixels != QRect(QPoint(), fullPixels(page))) {
        // Only the part in view; it stays over the page until the zoom changes.
        // A cover already shown for this zoom stays as it is.
        if (!m_tiles.hasCover(page))
            m_tiles.cover(lbl, page, it->image, it->pixels, m_canvas->devicePixelRatioF());
        m_tiles.dropStandIns(page);
    } else if (!tiled(page)) {
        m_tiles.remove(page);
        // The level becomes the page's render; the zoom before stays as a level.
        QImage image = it->image;
        m_levels.erase(it);
        showBase(page, std::move(image));
        m_staleBase.remove(page);
        if (!editedOnPage(page)) m_changed.insert(page, { version, QRect() });
    } else {
        m_tiles.adopt(lbl, page, it->pixels, it->image, m_canvas->devicePixelRatioF(), m_zoom,
                      version, fullPixels(page));
        m_tiles.dropStandIns(page);
    }
    m_anchors.insert(page, { deviceScale(), stateFor(page), {}, {}, {} });
    return true;
}
#endif
