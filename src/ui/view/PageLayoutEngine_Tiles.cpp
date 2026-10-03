#include "ui/view/PageLayoutEngine.hpp"
#include "ui/view/PageView.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/PdfBackend.hpp"
#  include "engine/render/PdfRenderer.hpp"
#endif

#include <QLabel>
#include <QWidget>

#include <cmath>

namespace {

    // Pages larger than this get a capped render plus sharp tiles of the
    // visible part instead of one render of the whole page.
    constexpr qint64 kWholePagePixels = 4'000'000;

    // The render under the tiles of such a page; large enough that zooming out
    // shows it downscaled, so sharp.
    constexpr qint64 kBasePixels = 8'000'000;
}

#ifdef HAVE_PDF_RENDERING
qreal PageLayoutEngine::deviceScale() const
{
    return PdfRenderer::screenScale(m_zoom) * m_canvas->devicePixelRatioF();
}

bool PageLayoutEngine::tiled(int page) const
{
    return tiledAt(page, deviceScale());
}

bool PageLayoutEngine::tiledAt(int page, qreal scale) const
{
    const QSize full = m_renderer ? m_renderer->backend()->pixelSize(page, scale) : QSize();
    return qint64(full.width()) * full.height() > kWholePagePixels;
}

qreal PageLayoutEngine::baseScale(int page) const
{
    if (!tiled(page)) return deviceScale();
    const QSize full = m_renderer->backend()->pixelSize(page, deviceScale());
    return deviceScale() * qMin(1.0, std::sqrt(qreal(kBasePixels) / (qreal(full.width()) * full.height())));
}

QRect PageLayoutEngine::wantedTile(int page, bool withMargin) const
{
    const QLabel *lbl = m_pageLabels.value(page, nullptr);
    if (!lbl || m_visibleRect.isEmpty()) return {};
    QRect window = m_visibleRect;
    if (withMargin) {
        // A full view ahead in the direction of scrolling, a little elsewhere.
        const int w = m_visibleRect.width(), h = m_visibleRect.height();
        const auto ahead = [](int direction, int size, bool forward) {
            if (direction == 0) return size / 2;
            return (direction > 0) == forward ? size : size / 4;
        };
        window.adjust(-ahead(m_scrollDirection.x(), w, false), -ahead(m_scrollDirection.y(), h, false),
                      ahead(m_scrollDirection.x(), w, true), ahead(m_scrollDirection.y(), h, true));
    }
    const QRect local = window.intersected(lbl->geometry()).translated(-lbl->pos())
                              .intersected(lbl->contentsRect());
    if (local.isEmpty()) return {};
    const qreal dpr = m_canvas->devicePixelRatioF();
    return QRectF(QPointF(local.topLeft()) * dpr, QSizeF(local.size()) * dpr).toAlignedRect()
               .intersected(QRect(QPoint(), fullPixels(page)));
}

QSize PageLayoutEngine::fullPixels(int page) const
{
    return m_renderer ? m_renderer->backend()->pixelSize(page, deviceScale()) : QSize();
}

QList<QRect> PageLayoutEngine::missingCells(int page, bool withMargin) const
{
    QList<QRect> missing;
    const QLabel *lbl = m_pageLabels.value(page, nullptr);
    if (!lbl) return missing;
    const int version = m_pageVersions.value(page);
    for (const QRect &cell : PageTiles::cells(wantedTile(page, withMargin), fullPixels(page)))
        if (!m_tiles.has(page, cell, m_zoom, version)) missing.append(cell);
    // Nearest the middle of the view first.
    const QPointF centre = QPointF(m_visibleRect.center() - lbl->pos())
                         * m_canvas->devicePixelRatioF();
    std::stable_sort(missing.begin(), missing.end(), [centre](const QRect &a, const QRect &b) {
        return QLineF(QPointF(a.center()), centre).length()
             < QLineF(QPointF(b.center()), centre).length();
    });
    return missing;
}

void PageLayoutEngine::restoreTiles(int page, const QRect &pixels)
{
    // Tiles seen before at this zoom come back instead of being rendered again.
    QLabel *lbl = m_pageLabels.value(page, nullptr);
    if (!lbl || pixels.isEmpty()) return;
    const int version = m_pageVersions.value(page);
    bool restored = false;
    for (const QRect &cell : PageTiles::cells(pixels, fullPixels(page)))
        if (!m_tiles.has(page, cell, m_zoom, version))
            restored |= m_tiles.restore(lbl, page, cell, m_canvas->devicePixelRatioF(), m_zoom,
                                        version);
    if (restored && m_tiles.covers(page, wantedTile(page, false), fullPixels(page), m_zoom, version))
        m_tiles.dropStandIns(page);
}

bool PageLayoutEngine::sharp(int page) const
{
    const auto it = m_rendered.constFind(page);
    const int version = m_pageVersions.value(page);
    if (it == m_rendered.cend() || it->zoom != m_zoom || it->version != version) return false;
    return !tiled(page)
        || m_tiles.covers(page, wantedTile(page, false), fullPixels(page), m_zoom, version);
}

bool PageLayoutEngine::blank(int page) const
{
    const QLabel *lbl = m_pageLabels.value(page, nullptr);
    return lbl && !PageView::hasPicture(lbl);
}

void PageLayoutEngine::showBase(int page, QImage image)
{
    QLabel *lbl = m_pageLabels.value(page, nullptr);
    if (!lbl || image.isNull()) return;
    // Takes the render's pixels over instead of copying them.
    QPixmap pm = QPixmap::fromImage(std::move(image));
    if (tiled(page)) {
        // A capped render stretched over the page; the tile shows it sharp.
        pm.setDevicePixelRatio(qreal(pm.width()) / qMax(1, lbl->width()));
    } else {
        pm.setDevicePixelRatio(m_canvas->devicePixelRatioF());
        const QSize logical = (QSizeF(pm.size()) / pm.devicePixelRatio()).toSize();
        if (!logical.isEmpty() && logical != lbl->size()) lbl->setFixedSize(logical);
    }
    // The whole page at the zoom before stays as that zoom's level, so zooming
    // back shows it at once.
    const int version = m_pageVersions.value(page);
    const auto old = m_rendered.constFind(page);
    if (old != m_rendered.cend() && old->zoom != m_zoom && old->version == version) {
        const qreal oldScale = PdfRenderer::screenScale(old->zoom) * m_canvas->devicePixelRatioF();
        const QSize full = m_renderer->backend()->pixelSize(page, oldScale);
        if (old->pixmap.size() == full && !m_levels.contains(levelKey(page, old->zoom)))
            m_levels.insert(levelKey(page, old->zoom),
                            { version, QRect(QPoint(), full), old->pixmap.toImage() });
    }
    PageView::setPicture(lbl, pm);
    m_rendered.insert(page, { pm, m_zoom, version });
}
#endif
