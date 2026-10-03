#include "ui/view/PageLayoutEngine.hpp"
#include "ui/view/PageView.hpp"

#include "app/SystemMemory.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentWorker.hpp"
#  include "engine/document/PdfBackend.hpp"
#  include "engine/edit/EditSession.hpp"
#  include "engine/render/PdfRenderer.hpp"
#endif

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFrame>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>


namespace {

    constexpr int kZoomRenderDelayMs   = 110;
}

PageLayoutEngine::PageLayoutEngine(QWidget *canvas, QVBoxLayout *layout,
                                   QWidget *gridCanvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
    , m_layout(layout)
    , m_gridCanvas(gridCanvas)
    , m_renderTimer(new QTimer(this))
{
    m_renderTimer->setSingleShot(true);
    connect(m_renderTimer, &QTimer::timeout, this, [this]() { renderPending(); });
    m_tiles.setKeptBytes(SystemMemory::cacheBytes() / 2);
    m_canvas->installEventFilter(this);

    m_staleBaseTimer = new QTimer(this);
    m_staleBaseTimer->setSingleShot(true);
    m_staleBaseTimer->setInterval(250);
    connect(m_staleBaseTimer, &QTimer::timeout, this, &PageLayoutEngine::catchUpBases);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    connect(m_refreshTimer, &QTimer::timeout, this, &PageLayoutEngine::refreshQueued);

    m_settleTimer = new QTimer(this);
    m_settleTimer->setSingleShot(true);
    m_settleTimer->setInterval(600);
    connect(m_settleTimer, &QTimer::timeout, this, [this]() { scheduleRender(0); });
}

#ifdef HAVE_PDF_RENDERING
void PageLayoutEngine::setSource(PdfRenderer *renderer, EditSession *session,
                                 DocumentWorker *worker)
{
    m_renderer = renderer;
    m_session  = session;
    m_worker   = worker;
    if (!m_worker) return;
    connect(m_worker, &DocumentWorker::pageRendered, this, &PageLayoutEngine::showRendered);
    connect(m_worker, &DocumentWorker::stopped, this, [this]() { scheduleRender(0); });
}
#endif

void PageLayoutEngine::clearPages()
{
    m_renderTimer->stop();
    m_rendered.clear();
    m_previews.clear();
    m_pageVersions.clear();
    m_changed.clear();
    m_changedPts.clear();
    m_refreshQueue.clear();
    m_settleLeft.clear();
#ifdef HAVE_PDF_RENDERING
    m_anchors.clear();
    m_levels.clear();
    m_levelAsked.clear();
    m_wholeRenderMs.clear();
#endif
    m_zoomWanted = 0;
    m_tiles.clear();
    m_staleBase.clear();
    m_inexact.clear();
    m_blank = {};
    for (QLabel *lbl : m_pageLabels) {
        m_layout->removeWidget(lbl);
        delete lbl;
    }
    m_pageLabels.clear();
    setPending({});
}

void PageLayoutEngine::buildPages()
{
    clearPages();

    for (int i = 0; i < m_pageCount; ++i) {
        auto *lbl = new PageView(m_canvas);
        lbl->setObjectName(QStringLiteral("PageLabel"));
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setFrameStyle(QFrame::Box | QFrame::Plain);
        lbl->setLineWidth(1);
        lbl->setAutoFillBackground(true);
        QPalette p = lbl->palette();
        p.setColor(QPalette::Window, Qt::white);
        lbl->setPalette(p);
        lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_layout->addWidget(lbl, 0, Qt::AlignHCenter);
        m_pageLabels.append(lbl);
        m_pageVersions.append(++m_versionCounter);
    }

    m_visibleRect.moveTo(0, 0);
    resizePages();
    // The new labels are shown and laid out by queued events; rendering before
    // that would see every page at the origin and render all of them.
    if (!m_pageLabels.isEmpty()) setPending({ -1 });
    scheduleRender(0);
}

void PageLayoutEngine::setZoom(int percent)
{
    if (percent <= 0 || percent == m_zoom) return;
    const auto [first, last] = visibleRange(0);
    // The sharp tiles stay, stretched, until the ones for the new zoom are there.
    m_tiles.rescale(qreal(percent) / m_zoom);
    m_zoomDirection = percent > m_zoom ? 1 : -1;
    m_zoom = percent;
    m_staleBase.clear();
    resizePages();
#ifdef HAVE_PDF_RENDERING
    m_levelShown = last >= first;
    for (int page = first; page <= last; ++page)
        if (!adoptLevel(page)) m_levelShown = false;
    // Zooming out brings pages into view that were prepared for it.
    QList<int> more;
    for (auto it = m_levels.cbegin(); it != m_levels.cend(); ++it) {
        const int page = int(it.key() >> 16);
        if (int(it.key() & 0xffff) == m_zoom && (page < first || page > last)) more.append(page);
    }
    for (int page : std::as_const(more)) adoptLevel(page);
    // A zoom seen before brings its tiles back.
    for (int page = first; page <= last; ++page)
        if (tiled(page)) restoreTiles(page, QRect(QPoint(), fullPixels(page)));
#endif
    // Shown exactly already, the next level is prepared at once.
    scheduleRender(m_levelShown ? 0 : kZoomRenderDelayMs);
}

void PageLayoutEngine::setVisibleRect(const QRect &canvasRect)
{
    if (canvasRect == m_visibleRect) return;
    if (!m_visibleRect.isEmpty()) {
        const QPoint moved = canvasRect.topLeft() - m_visibleRect.topLeft();
        if (moved.x() != 0) m_scrollDirection.setX(moved.x() > 0 ? 1 : -1);
        if (moved.y() != 0) m_scrollDirection.setY(moved.y() > 0 ? 1 : -1);
    }
    m_visibleRect = canvasRect;
#ifdef HAVE_PDF_RENDERING
    // Tiles seen before are back in the same frame, not a render later.
    if (m_renderer && !m_gridActive) {
        const auto [first, last] = visibleRange(0);
        for (int page = first; page <= last; ++page)
            if (tiled(page)) restoreTiles(page, wantedTile(page, false));
    }
#endif
    scheduleRender(kScrollRenderDelayMs);
}

void PageLayoutEngine::scheduleRender(int delayMs)
{
    if (m_pageLabels.isEmpty()) return;

    if (m_renderTimer->isActive() && m_renderTimer->remainingTime() <= delayMs)
        return;
    m_renderTimer->start(delayMs);
}

void PageLayoutEngine::resizePages()
{
#ifdef HAVE_PDF_RENDERING
    if (!m_renderer) return;

    const auto [first, last] = visibleRange();
    for (auto it = m_rendered.begin(); it != m_rendered.end(); ) {
        if (it->zoom == m_zoom || (it.key() >= first && it.key() <= last)) { ++it; continue; }
        if (QLabel *lbl = m_pageLabels.value(it.key(), nullptr)) PageView::setPicture(lbl, QPixmap());
        it = m_rendered.erase(it);
    }

    for (int i = 0; i < m_pageLabels.size(); ++i) {
        const QSize sz = m_renderer->pageDisplaySize(i, m_zoom);
        if (sz.isEmpty()) continue;
        m_pageLabels[i]->setFixedSize(sz);
        if (m_rendered.contains(i)) showPlaceholder(i);
    }

    if (m_layout) m_layout->activate();
#endif
    Q_EMIT layoutChanged();
}

std::pair<int, int> PageLayoutEngine::visibleRange(int slackDivisor) const
{
    if (m_pageLabels.isEmpty()) return { 0, -1 };
    if (m_visibleRect.isEmpty())
        return m_pageLabels.first()->isHidden() ? std::pair { 0, -1 } : std::pair { 0, 0 };

    const int slack = slackDivisor > 0 ? m_visibleRect.height() / slackDivisor : 0;
    const QRect window = m_visibleRect.adjusted(0, -slack, 0, slack);

    int first = -1, last = -1;
    for (int i = 0; i < m_pageLabels.size(); ++i) {
        if (m_pageLabels[i]->isHidden()) continue;
        const QRect g = m_pageLabels[i]->geometry();
        if (g.bottom() < window.top() || g.top() > window.bottom()) continue;
        if (first < 0) first = i;
        last = i;
    }
    if (first >= 0) return { first, last };

    const int center = window.center().y();
    int best = -1, bestDist = std::numeric_limits<int>::max();
    for (int i = 0; i < m_pageLabels.size(); ++i) {
        if (m_pageLabels[i]->isHidden()) continue;
        const int d = qAbs(m_pageLabels[i]->geometry().center().y() - center);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    if (best < 0) return { 0, -1 };
    return { best, best };
}

int PageLayoutEngine::bumpVersion(int page)
{
    if (page < 0 || page >= m_pageVersions.size()) return 0;
    m_pageVersions[page] = ++m_versionCounter;
    m_tiles.dropCover(page);
    return m_pageVersions[page];
}

void PageLayoutEngine::setPending(QSet<int> pages)
{
    const bool was = !m_pending.isEmpty();
    m_pending = std::move(pages);
    if (was != !m_pending.isEmpty()) Q_EMIT busyChanged(!m_pending.isEmpty());
}



void PageLayoutEngine::renderPending()
{
#ifdef HAVE_PDF_RENDERING
    m_renderTimer->stop();
    if (m_gridActive) {
        renderGrid();
        return;
    }
    if (!m_renderer || m_pageLabels.isEmpty()) return;
    // Until the layout has placed new labels they all sit at the origin, and
    // the pages in the middle of the document would look visible.
    const QLabel *beforeLast = m_pageLabels.value(m_pageLabels.size() - 2, nullptr);
    if (m_canvas->isVisible() && beforeLast
            && m_pageLabels.last()->y() < beforeLast->geometry().bottom()) {
        scheduleRender(5);
        return;
    }
    catchUpBases();

    const auto [first, last] = m_canvas->isVisible() ? visibleRange() : std::pair { 0, -1 };
    if (last < first) {
        if (m_worker) m_worker->setRenders({});
        setPending({});
        return;
    }
    const auto [shownFirst, shownLast] = visibleRange(0);
    m_tiles.removeOutside(first, last);

    QList<int> order;
    for (int i = first; i <= last; ++i) order.append(i);
    const int centre = (shownFirst + shownLast) / 2;
    if (m_worker) m_worker->setFocusPage(centre);
    std::stable_sort(order.begin(), order.end(), [centre](int a, int b) {
        return qAbs(a - centre) < qAbs(b - centre);
    });

    std::shared_ptr<const EditSession> snapshot;
    QList<DocumentWorker::Render> tilesNow, tilesSoon, bases, neighbours;
    bool changed = false;
    const auto request = [this, &snapshot](int page) {
        DocumentWorker::Render render;
        render.page    = page;
        render.zoom    = m_zoom;
        render.version = m_pageVersions.value(page);
        // The page being edited renders with what the editor shows.
        if (m_blank.page == page || m_preview.page == page) {
            render.session = std::make_shared<EditSession>(stateFor(page));
        } else if (editedOnPage(page)) {
            if (!snapshot) snapshot = std::make_shared<EditSession>(*m_session);
            render.session = snapshot;
        }
        return render;
    };

    for (int page : std::as_const(order)) {
        const int version = m_pageVersions.value(page);
        const auto it = m_rendered.constFind(page);
        const bool baseOk = it != m_rendered.cend() && it->zoom == m_zoom
                         && it->version == version;
        const bool isTiled = tiled(page);
        QList<QRect> visibleCells, marginCells;
        if (isTiled) {
            // Tiles sit in a fixed grid, so the ones already rendered stay
            // valid while scrolling and only the missing ones are asked for.
            const int keep = PageTiles::kSize;
            m_tiles.keepOnly(page, wantedTile(page, true).adjusted(-keep, -keep, keep, keep));
            restoreTiles(page, wantedTile(page, true));
            visibleCells = missingCells(page, false);
            marginCells  = missingCells(page, true);
        } else {
            m_tiles.keepOnly(page, QRect());
        }
        if (baseOk && marginCells.isEmpty()) continue;

        if (!m_worker) {
            if (!baseOk)                      renderNow(page);
            else if (!visibleCells.isEmpty()) renderTileNow(page);
            changed = true;
            continue;
        }

        DocumentWorker::Render render = request(page);
        render.scale = deviceScale();
        for (const QRect &cell : std::as_const(marginCells)) {
            render.area = cell;
            (visibleCells.contains(cell) ? tilesNow : tilesSoon).append(render);
        }
        if (!baseOk) {
            changed |= showPlaceholder(page);
            render.scale = baseScale(page);
            render.area  = QRect();
            bases.append(render);
        }
    }

    // The pages just outside get their base early, two in the direction of
    // scrolling, so they are not empty when scrolled into view.
    QList<int> around { last + 1, first - 1 };
    if (m_scrollDirection.y() > 0)      around = { last + 1, last + 2, first - 1 };
    else if (m_scrollDirection.y() < 0) around = { first - 1, first - 2, last + 1 };
    for (int page : std::as_const(around)) {
        if (!m_worker || page < 0 || page >= m_pageLabels.size()) continue;
        if (m_blank.page == page || m_preview.page == page) continue;
        const auto it = m_rendered.constFind(page);
        if (it != m_rendered.cend() && it->zoom == m_zoom
                && it->version == m_pageVersions.value(page))
            continue;
        DocumentWorker::Render render = request(page);
        render.scale = baseScale(page);
        neighbours.append(render);
    }
    // Pages patched while editing get an exact render once editing pauses.
    QList<DocumentWorker::Render> settle;
    if (m_worker && !m_settleTimer->isActive()) {
        for (int page : std::as_const(order)) {
            if (!m_inexact.contains(page) || !m_rendered.contains(page)) continue;
            DocumentWorker::Render render;
            render.page    = page;
            render.zoom    = m_zoom;
            render.version = m_pageVersions.value(page);
            render.session = std::make_shared<EditSession>(stateFor(page));
            // What is still missing of the exact render; asked again as it is.
            if (!m_settleLeft.contains(page)) {
                QList<QRect> parts { QRect() };
                if (tiled(page)) {
                    // Tiles out of view are dropped and come back fresh when needed.
                    const QRect shown = wantedTile(page, false);
                    m_tiles.keepOnly(page, shown);
                    parts += PageTiles::cells(shown, fullPixels(page));
                }
                m_settleLeft.insert(page, parts);
            }
            for (const QRect &part : std::as_const(m_settleLeft[page])) {
                render.scale = part.isNull() ? baseScale(page) : deviceScale();
                render.area  = part;
                settle.append(render);
            }
        }
    }
    m_settling = !settle.isEmpty();

    const bool viewDone = tilesNow.isEmpty() && bases.isEmpty() && settle.isEmpty();
    const ZoomRenders zoomRenders = planZoomRenders(first, last, shownFirst, shownLast, viewDone,
                                                    viewDone && tilesSoon.isEmpty(), request);
    const auto &[wanted, levelsNear, levelsFar] = zoomRenders;
    // While a level rendered for this zoom is on screen, the view is already
    // exact, and the levels ahead matter more than its tiles.
    // A zoom waiting to be shown sharp goes before everything.
    if (m_worker && m_levelShown)
        m_worker->setRenders(wanted + levelsNear + tilesNow + bases + tilesSoon + settle
                             + neighbours + levelsFar);
    else if (m_worker)
        m_worker->setRenders(wanted + tilesNow + bases + levelsNear + tilesSoon + settle
                             + neighbours + levelsFar);

    // The spinner is for pages that show nothing yet; a stand-in that is
    // still being sharpened needs none.
    QSet<int> pending;
    for (int page = shownFirst; page <= shownLast; ++page)
        if (blank(page)) pending.insert(page);
    setPending(std::move(pending));
    evictRendered(first, last);
    if (changed) Q_EMIT layoutChanged();
#endif
}

void PageLayoutEngine::settled(int page, const QRect &part)
{
    const auto left = m_settleLeft.find(page);
    if (left == m_settleLeft.end()) return;
    left->removeAll(part);
    if (!left->isEmpty()) return;
    m_settleLeft.erase(left);
    m_inexact.remove(page);
}

void PageLayoutEngine::showRendered(int page, int zoom, int version, const QRect &area,
                                    const QImage &image, int milliseconds)
{
#ifdef HAVE_PDF_RENDERING
    if (zoom == kGridTag) {
        showThumbnail(page, version, image);
        return;
    }
    if (keepZoomRender(page, zoom, version, area, image)) return;
    if (zoom != m_zoom || version != m_pageVersions.value(page, -1)) return;
    QLabel *lbl = m_pageLabels.value(page, nullptr);
    const bool editing = m_blank.page == page || m_preview.page == page;
    if (!lbl) return;

    if (!area.isEmpty()) {
        // A finished tile on screen stays: a second render of it only differs
        // in rounding. After editing, the exact render does replace it.
        if (m_tiles.has(page, area, zoom, version) && !m_inexact.contains(page)) {
            settled(page, area);
            return;
        }
        catchUpBase(page);
        m_tiles.show(lbl, page, area, image, m_canvas->devicePixelRatioF(), zoom, version);
        if (m_tiles.covers(page, wantedTile(page, false), fullPixels(page), zoom, version))
            m_tiles.dropStandIns(page);
        // A new tile replaces pixels the anchor of an edit may have kept.
        m_anchors.insert(page, { deviceScale(), stateFor(page), {}, {}, {} });
        if (editing) noteChanged(page, version, changedAt(page, deviceScale()), deviceScale());
        settled(page, area);
    } else {
        if (!editing) keepPreview(page, image);
        if (!tiled(page)) m_wholeRenderMs.insert(page, milliseconds);
        m_staleBase.remove(page);
        if (!m_settleLeft.contains(page)) m_inexact.remove(page);
        else settled(page, QRect());
        if (!m_inexact.contains(page)) m_anchors.insert(page, { deviceScale(), stateFor(page), {}, {}, {} });
        // The render is not used after this; its only receiver takes it over.
        showBase(page, std::move(const_cast<QImage &>(image)));
        if (!editedOnPage(page))
            noteChanged(page, version, QRect(), baseScale(page));
        else if (editing)
            noteChanged(page, version, changedAt(page, baseScale(page)), baseScale(page));
        else if (m_changed.value(page).version != version)
            m_changed.remove(page);
    }

    if (sharp(page)) m_tiles.dropStandIns(page);
    QSet<int> pending = m_pending;
    if (!blank(page)) pending.remove(page);
    setPending(std::move(pending));
    const auto [first, last] = visibleRange();
    evictRendered(first, last);
    if (m_pending.isEmpty() && sharp(page)) scheduleRender(60);

    Q_EMIT layoutChanged();
#else
    Q_UNUSED(page) Q_UNUSED(zoom) Q_UNUSED(version) Q_UNUSED(area) Q_UNUSED(image)
    Q_UNUSED(milliseconds)
#endif
}

bool PageLayoutEngine::showPlaceholder(int page)
{
    QLabel *lbl = m_pageLabels.value(page, nullptr);
    if (!lbl || lbl->size().isEmpty()) return false;

    QPixmap source;
    const auto it = m_rendered.constFind(page);
    if (it != m_rendered.cend()) {
        if (it->zoom == m_zoom) return false;
        source = it->pixmap;
    } else {
        source = m_previews.value(page);
    }
    if (source.isNull()) return false;

    // Stretched while painting; scaling it here cost a full-size copy per step.
    QPixmap placeholder = source;
    placeholder.setDevicePixelRatio(qreal(source.width()) / lbl->width());
    PageView::setPicture(lbl, placeholder);
    m_rendered.insert(page, { placeholder, m_zoom, 0 });
    return true;
}

void PageLayoutEngine::keepPreview(int page, const QImage &image)
{
    constexpr int kPreviewWidth = 200;
    if (image.isNull()) return;
    // A large render is first sampled down roughly, so the smooth step that
    // follows only reads a few pixels.
    const QImage rough = image.width() > kPreviewWidth * 4
        ? image.scaledToWidth(kPreviewWidth * 4, Qt::FastTransformation) : image;
    m_previews.insert(page, QPixmap::fromImage(
        rough.scaledToWidth(qMin(kPreviewWidth, rough.width()), Qt::SmoothTransformation)));
}

void PageLayoutEngine::evictRendered(int first, int last)
{
    // Pages seen before stay, so scrolling back never renders them again.
    const qint64 kCacheBytes = SystemMemory::cacheBytes() / 2;
    const auto bytes = [](const QPixmap &pm) {
        return qint64(pm.width()) * pm.height() * 4;
    };

    qint64 total = 0;
    QList<int> outside;
    for (auto it = m_rendered.cbegin(); it != m_rendered.cend(); ++it) {
        total += bytes(it->pixmap);
        if (it.key() < first || it.key() > last) outside.append(it.key());
    }
    if (total <= kCacheBytes) return;

    const int centre = (first + last) / 2;
    std::sort(outside.begin(), outside.end(), [centre](int a, int b) {
        return qAbs(a - centre) > qAbs(b - centre);
    });
    for (int page : std::as_const(outside)) {
        if (total <= kCacheBytes) break;
        total -= bytes(m_rendered.value(page).pixmap);
        m_rendered.remove(page);
        if (QLabel *lbl = m_pageLabels.value(page, nullptr)) PageView::setPicture(lbl, QPixmap());
    }
}

void PageLayoutEngine::renderNow(int page)
{
#ifdef HAVE_PDF_RENDERING
    QLabel *lbl = m_pageLabels.value(page, nullptr);
    if (!m_renderer || !m_renderer->backend() || !lbl) return;

    const int version  = bumpVersion(page);
    const bool editing = m_blank.page == page || m_preview.page == page;
    const EditSession state = stateFor(page);
    const EditSession *use  = editedOnPage(page) ? &state : nullptr;

    QElapsedTimer clock;
    clock.start();
    const PdfBackend::AreaRender render =
        m_renderer->backend()->renderChanges(page, baseScale(page), use, {}, true);
    if (!tiled(page)) m_wholeRenderMs.insert(page, int(clock.elapsed()));
    if (render.image.isNull()) {
        PageView::setPicture(lbl, QPixmap());
        m_rendered.remove(page);
        m_changed.remove(page);
        m_tiles.remove(page);
        return;
    }
    if (!editing) keepPreview(page, render.image);
    m_staleBase.remove(page);
    m_inexact.remove(page);
    m_settleLeft.remove(page);
    m_anchors.insert(page, { deviceScale(), state, {}, {}, {} });
    showBase(page, render.image);
    if (tiled(page)) {
        m_changed.remove(page);
        renderTileNow(page);
    } else {
        m_tiles.remove(page);
        noteChanged(page, version, render.changed, baseScale(page));
    }
    if (sharp(page)) m_tiles.dropStandIns(page);
    if (m_pending.contains(page) && !blank(page)) {
        QSet<int> pending = m_pending;
        pending.remove(page);
        setPending(std::move(pending));
    }
#else
    Q_UNUSED(page)
#endif
}

void PageLayoutEngine::renderTileNow(int page)
{
#ifdef HAVE_PDF_RENDERING
    QLabel *lbl = m_pageLabels.value(page, nullptr);
    if (!lbl || !m_renderer) return;
    const QList<QRect> missing = missingCells(page, false);
    if (missing.isEmpty()) return;
    QRect need;
    for (const QRect &cell : missing) need = need.united(cell);
    catchUpBase(page);

    const EditSession state = stateFor(page);
    const PdfBackend::AreaRender render = m_renderer->backend()->renderChanges(
        page, deviceScale(), editedOnPage(page) ? &state : nullptr, need, false);
    if (render.image.isNull()) return;
    const int version = m_pageVersions.value(page);
    for (const QRect &cell : missing) {
        if (!render.pixels.contains(cell)) continue;
        m_tiles.show(lbl, page, cell, render.image.copy(cell.translated(-render.pixels.topLeft())),
                     m_canvas->devicePixelRatioF(), m_zoom, version);
    }
    noteChanged(page, version, render.changed, deviceScale());
    if (sharp(page)) m_tiles.dropStandIns(page);
#else
    Q_UNUSED(page)
#endif
}

void PageLayoutEngine::rerenderAll()
{
    for (int i = 0; i < m_pageVersions.size(); ++i) bumpVersion(i);
    resizePages();
    renderPending();
}

void PageLayoutEngine::rerenderPage(int page)
{
    if (page < 0 || page >= m_pageLabels.size()) return;
    m_refreshQueue.remove(page);
    if (m_blank.page == page) m_blank = {};
    if (m_preview.page == page) m_preview = {};
    m_previews.remove(page);
    const auto [first, last] = visibleRange();

    if (page >= first && page <= last) {
        // Only the changed part is rendered again; a change that is not a page
        // object, like a link, makes the backend render the page whole.
        refreshPage(page);
        return;
    }
    m_rendered.remove(page);
    m_tiles.remove(page);
    m_staleBase.remove(page);
    bumpVersion(page);
    PageView::setPicture(m_pageLabels[page], QPixmap());
}

bool PageLayoutEngine::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == m_canvas && e->type() == QEvent::Show) scheduleRender(0);

    if (m_gridActive && e->type() == QEvent::MouseButtonRelease) {
        auto it = m_gridCardIndex.constFind(obj);
        if (it != m_gridCardIndex.cend()) {
            if (static_cast<QMouseEvent *>(e)->button() == Qt::LeftButton)
                Q_EMIT pageActivated(it.value());
            return true;
        }
    }
    return QObject::eventFilter(obj, e);
}
