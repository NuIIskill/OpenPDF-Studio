#include "ui/view/PageTiles.hpp"

#include <QFrame>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>

namespace {

/// Shows one tile, from its own image or from a part of a larger picture it shares.
class TileView : public QWidget
{
public:
    explicit TileView(QWidget *parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("PageTile"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

    QImage image;
    QRect  source;
    QRect  valid;
    qreal  dpr { 1.0 };
    bool   standIn { false };
    QRectF exact;

    void place(const QRectF &geometry)
    {
        exact = geometry;
        setGeometry(geometry.toAlignedRect());
    }

    // The tile's own pixels, so they can be changed in place.
    void own()
    {
        if (source.topLeft() == QPoint() && image.size() == source.size()) return;
        QImage mine(source.size(), QImage::Format_RGB32);
        mine.fill(Qt::white);
        QPainter painter(&mine);
        painter.drawImage(QPoint(), image, source);
        painter.end();
        image  = mine;
        source = mine.rect();
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        QPainter painter(this);
        // The device pixels of the tile shown in its own coordinates.
        const QRectF tile = standIn ? exact.translated(-QPointF(pos()))
                                    : QRectF(QPointF(), QSizeF(source.size()) / dpr);
        const qreal sx = tile.width() / qMax(1, source.width());
        const qreal sy = tile.height() / qMax(1, source.height());
        QRect from = source.intersected(image.rect());
        if (!valid.isNull()) from = from.intersected(valid.translated(source.topLeft()));
        if (from.isEmpty()) return;
        const QRectF to(tile.left() + (from.left() - source.left()) * sx,
                        tile.top() + (from.top() - source.top()) * sy,
                        from.width() * sx, from.height() * sy);
        if (standIn) {
            // A tile of the previous zoom, stretched smoothly to exactly where
            // it belongs, so nothing shifts when the new one replaces it.
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(to, image, from);
            return;
        }
        painter.setClipRect(e->rect());
        painter.drawImage(to, image, from);
    }
};

TileView *view(const QPointer<QWidget> &widget)
{
    return static_cast<TileView *>(widget.data());
}

}

PageTiles::~PageTiles()
{
    clear();
    for (const QPointer<QWidget> &widget : std::as_const(m_spare)) delete widget.data();
}

QWidget *PageTiles::take(QLabel *pageLabel)
{
    // Widgets are reused, so a zoom step does not create and destroy dozens.
    while (!m_spare.isEmpty()) {
        QPointer<QWidget> widget = m_spare.takeLast();
        if (!widget) continue;
        if (widget->parentWidget() != pageLabel) widget->setParent(pageLabel);
        widget->raise();
        return widget;
    }
    return new TileView(pageLabel);
}

void PageTiles::release(QWidget *widget)
{
    if (!widget) return;
    constexpr int kSpare = 256;
    if (m_spare.size() >= kSpare) {
        delete widget;
        return;
    }
    TileView *v = static_cast<TileView *>(widget);
    v->hide();
    v->image   = QImage();
    v->source  = QRect();
    v->valid   = QRect();
    v->standIn = false;
    v->setAttribute(Qt::WA_OpaquePaintEvent, true);
    m_spare.append(widget);
}

quint64 PageTiles::key(const QRect &cell)
{
    return (quint64(quint32(cell.x() / kSize)) << 32) | quint32(cell.y() / kSize);
}

quint64 PageTiles::keptKey(int page, int zoom, const QRect &cell)
{
    return (quint64(quint16(page)) << 48) | (quint64(quint16(zoom)) << 32)
         | (quint64(quint16(cell.x() / kSize)) << 16) | quint16(cell.y() / kSize);
}

void PageTiles::keep(int page, const Tile &tile)
{
    // Only finished tiles; the oldest go first once the budget is used up.
    if (!tile.widget || !tile.partial.isNull() || m_keptLimit <= 0) return;
    const TileView *v = view(tile.widget);
    if (v->image.isNull()) return;
    const auto bytes = [](const Kept &k) { return qint64(k.source.width()) * k.source.height() * 4; };
    const quint64 k = keptKey(page, tile.zoom, tile.pixels);
    if (const auto old = m_kept.constFind(k); old != m_kept.cend()) {
        m_keptSize -= bytes(*old);
        m_keptOrder.removeOne(k);
    }
    const Kept kept { v->image, v->source, tile.pixels, tile.version };
    m_kept.insert(k, kept);
    m_keptOrder.append(k);
    m_keptSize += bytes(kept);
    while (m_keptSize > m_keptLimit && !m_keptOrder.isEmpty()) {
        const auto oldest = m_kept.constFind(m_keptOrder.takeFirst());
        if (oldest == m_kept.cend()) continue;
        m_keptSize -= bytes(*oldest);
        m_kept.erase(oldest);
    }
}

bool PageTiles::keptCovers(int page, int zoom, int version, const QRect &pixels,
                           const QSize &pageSize) const
{
    for (const QRect &cell : cells(pixels, pageSize)) {
        if (has(page, cell, zoom, version)) continue;
        const auto it = m_kept.constFind(keptKey(page, zoom, cell));
        if (it == m_kept.cend() || it->version != version || it->pixels != cell) return false;
    }
    return true;
}

bool PageTiles::restore(QLabel *pageLabel, int page, const QRect &cell, qreal dpr, int zoom,
                        int version)
{
    const quint64 k = keptKey(page, zoom, cell);
    const auto it = m_kept.constFind(k);
    if (it == m_kept.cend()) return false;
    const Kept kept = *it;
    m_keptSize -= qint64(kept.source.width()) * kept.source.height() * 4;
    m_kept.erase(it);
    m_keptOrder.removeOne(k);
    if (kept.version != version || kept.pixels != cell) return false;
    show(pageLabel, page, cell, kept.image, dpr, zoom, version, kept.source);
    return true;
}

QList<QRect> PageTiles::cells(const QRect &pixels, const QSize &pageSize)
{
    QList<QRect> out;
    const QRect page(QPoint(), pageSize);
    const QRect area = pixels.intersected(page);
    if (area.isEmpty()) return out;
    for (int y = area.top() / kSize; y <= area.bottom() / kSize; ++y)
        for (int x = area.left() / kSize; x <= area.right() / kSize; ++x)
            out.append(QRect(x * kSize, y * kSize, kSize, kSize).intersected(page));
    return out;
}

void PageTiles::show(QLabel *pageLabel, int page, const QRect &cell, const QImage &image,
                     qreal dpr, int zoom, int version, const QRect &source)
{
    if (!pageLabel || image.isNull()) return;
    Tile &tile = m_tiles[page][key(cell)];
    // A child of the page sits under every layer the canvas lays over it.
    if (!tile.widget) tile.widget = take(pageLabel);
    TileView *v = view(tile.widget);
    const QRect from = source.isNull() ? image.rect() : source;
    if (!tile.partial.isNull() && tile.zoom == zoom && tile.version == version) {
        // The part taken from a picture of the whole view stays as shown.
        QImage merged(cell.size(), QImage::Format_RGB32);
        QPainter painter(&merged);
        painter.drawImage(QPoint(), image, from);
        const QRect keep = tile.partial.translated(-cell.topLeft());
        painter.drawImage(keep.topLeft(), v->image, keep.translated(v->source.topLeft()));
        painter.end();
        v->image  = merged;
        v->source = merged.rect();
    } else {
        v->image  = image;
        v->source = from;
    }
    tile.partial = {};
    v->valid = QRect();
    v->standIn = false;
    v->setAttribute(Qt::WA_OpaquePaintEvent, true);
    v->dpr   = dpr;
    v->place(QRectF(QPointF(cell.topLeft()) / dpr, QSizeF(cell.size()) / dpr));
    // While stand-ins cover the page after a zoom, the new tiles wait and
    // appear together, instead of the page sharpening tile by tile.
    if (!m_standIns.contains(page)) v->show();
    v->update();
    tile.pixels  = cell;
    tile.zoom    = zoom;
    tile.version = version;
}

void PageTiles::patch(int page, const QRect &pixels, const QImage &image, int version,
                      const QImage &before)
{
    const bool masked = !before.isNull() && before.size() == image.size();
    const auto tiles = m_tiles.find(page);
    if (tiles == m_tiles.end()) return;
    for (Tile &tile : *tiles) {
        tile.version = version;
        if (image.isNull() || !tile.widget) continue;
        const QRect overlap = tile.pixels.intersected(pixels);
        if (overlap.isEmpty()) continue;
        TileView *v = view(tile.widget);
        v->own();
        const QPoint at = overlap.topLeft() - tile.pixels.topLeft();
        if (masked && v->image.format() == QImage::Format_RGB32
                && image.format() == QImage::Format_RGB32) {
            // Only pixels the change touches; the rest stays as rendered.
            const QPoint from = overlap.topLeft() - pixels.topLeft();
            for (int y = 0; y < overlap.height(); ++y) {
                QRgb *out = reinterpret_cast<QRgb *>(v->image.scanLine(at.y() + y)) + at.x();
                const QRgb *now = reinterpret_cast<const QRgb *>(image.constScanLine(from.y() + y)) + from.x();
                const QRgb *was = reinterpret_cast<const QRgb *>(before.constScanLine(from.y() + y)) + from.x();
                for (int x = 0; x < overlap.width(); ++x)
                    if (now[x] != was[x]) out[x] = now[x];
            }
        } else {
            QPainter painter(&v->image);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawImage(at, image, overlap.translated(-pixels.topLeft()));
        }
        v->update(QRectF(QPointF(at) / v->dpr, QSizeF(overlap.size()) / v->dpr)
                      .toAlignedRect().adjusted(-1, -1, 1, 1));
    }
}

void PageTiles::capture(int page, const QRect &pixels, QImage &into) const
{
    const auto tiles = m_tiles.constFind(page);
    if (tiles == m_tiles.cend()) return;
    QPainter painter(&into);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (const Tile &tile : *tiles) {
        const QRect overlap = tile.pixels.intersected(pixels);
        if (overlap.isEmpty() || !tile.widget) continue;
        const TileView *v = view(tile.widget);
        painter.drawImage(overlap.topLeft() - pixels.topLeft(), v->image,
                          overlap.translated(v->source.topLeft() - tile.pixels.topLeft()));
    }
}

bool PageTiles::has(int page, const QRect &cell, int zoom, int version) const
{
    const auto tiles = m_tiles.constFind(page);
    if (tiles == m_tiles.cend()) return false;
    const auto it = tiles->constFind(key(cell));
    return it != tiles->cend() && it->widget && it->zoom == zoom && it->version == version
        && it->pixels == cell && it->partial.isNull();
}

bool PageTiles::covers(int page, const QRect &pixels, const QSize &pageSize, int zoom,
                       int version) const
{
    for (const QRect &cell : cells(pixels, pageSize))
        if (!has(page, cell, zoom, version)) return false;
    return true;
}

bool PageTiles::current(int page, int zoom, int version) const
{
    const auto tiles = m_tiles.constFind(page);
    if (tiles == m_tiles.cend() || tiles->isEmpty()) return false;
    for (const Tile &tile : *tiles)
        if (!tile.widget || tile.zoom != zoom || tile.version != version) return false;
    return true;
}

void PageTiles::keepOnly(int page, const QRect &pixels)
{
    const auto tiles = m_tiles.find(page);
    if (tiles == m_tiles.end()) return;
    for (auto it = tiles->begin(); it != tiles->end(); ) {
        if (it->pixels.intersects(pixels)) { ++it; continue; }
        keep(page, *it);
        release(it->widget.data());
        it = tiles->erase(it);
    }
}

void PageTiles::rescale(qreal ratio)
{
    for (auto it = m_covers.begin(); it != m_covers.end(); it = m_covers.erase(it)) {
        if (!it.value()) continue;
        dropStandIns(it.key());
        view(it.value())->standIn = true;
        m_standIns[it.key()].append(it.value());
    }
    for (auto page = m_tiles.begin(); page != m_tiles.end(); ++page) {
        if (page->isEmpty()) continue;
        // Only the newest generation stands in; older stand-ins go.
        dropStandIns(page.key());
        QList<QPointer<QWidget>> &standIns = m_standIns[page.key()];
        for (const Tile &tile : std::as_const(*page)) {
            if (!tile.widget) continue;
            // Zooming back finds them again.
            keep(page.key(), tile);
            view(tile.widget)->standIn = true;
            standIns.append(tile.widget);
        }
        page->clear();
    }
    for (const QList<QPointer<QWidget>> &standIns : std::as_const(m_standIns))
        for (const QPointer<QWidget> &widget : standIns) {
            if (!widget) continue;
            TileView *v = view(widget);
            v->setAttribute(Qt::WA_OpaquePaintEvent, false);
            v->place(QRectF(v->exact.topLeft() * ratio, v->exact.size() * ratio));
            v->lower();
        }
}

void PageTiles::adopt(QLabel *pageLabel, int page, const QRect &area, const QImage &image,
                      qreal dpr, int zoom, int version, const QSize &pageSize)
{
    if (!pageLabel || image.isNull()) return;
    for (const QRect &cell : cells(area, pageSize)) {
        if (has(page, cell, zoom, version)) continue;
        const QRect overlap = cell.intersected(area);
        Tile &tile = m_tiles[page][key(cell)];
        // What a tile already shows stays; the picture only fills its gaps.
        if (tile.widget && !tile.partial.isNull() && tile.zoom == zoom && tile.version == version) {
            if (tile.partial.contains(overlap)) continue;
            TileView *v = view(tile.widget);
            QImage merged(cell.size(), QImage::Format_ARGB32_Premultiplied);
            merged.fill(Qt::transparent);
            QPainter painter(&merged);
            painter.drawImage(overlap.topLeft() - cell.topLeft(), image,
                              overlap.translated(-area.topLeft()));
            const QRect keep = tile.partial.translated(-cell.topLeft());
            painter.drawImage(keep.topLeft(), v->image, keep.translated(v->source.topLeft()));
            painter.end();
            const QRect covered = overlap.united(tile.partial);
            v->image  = merged;
            v->source = merged.rect();
            v->valid  = covered.translated(-cell.topLeft());
            tile.partial = covered == cell ? QRect() : covered;
            v->setAttribute(Qt::WA_OpaquePaintEvent, tile.partial.isNull());
            v->update();
            continue;
        }
        // The tile shows its part of the picture without a copy of it.
        tile.partial = {};
        show(pageLabel, page, cell, image, dpr, zoom, version,
             cell.translated(-area.topLeft()));
        if (overlap == cell) continue;
        tile.partial = overlap;
        TileView *v = view(tile.widget);
        v->valid = overlap.translated(-cell.topLeft());
        v->setAttribute(Qt::WA_OpaquePaintEvent, false);
    }
}

void PageTiles::cover(QLabel *pageLabel, int page, const QImage &image, const QRect &pixels,
                      qreal dpr)
{
    if (!pageLabel || image.isNull()) return;
    QPointer<QWidget> &widget = m_covers[page];
    if (!widget) widget = take(pageLabel);
    TileView *v = view(widget);
    v->image   = image;
    v->source  = image.rect();
    v->valid   = QRect();
    v->dpr     = dpr;
    v->standIn = false;
    v->place(QRectF(QPointF(pixels.topLeft()) / dpr, QSizeF(pixels.size()) / dpr));
    v->show();
    v->raise();
    v->update();
}

void PageTiles::dropCover(int page)
{
    if (const auto it = m_covers.find(page); it != m_covers.end()) {
        release(it->data());
        m_covers.erase(it);
    }
}

void PageTiles::dropStandIns(int page)
{
    if (const auto tiles = m_tiles.constFind(page); tiles != m_tiles.cend())
        for (const Tile &tile : *tiles)
            if (tile.widget) tile.widget->show();
    const auto it = m_standIns.find(page);
    if (it == m_standIns.end()) return;
    for (const QPointer<QWidget> &widget : std::as_const(*it)) release(widget.data());
    m_standIns.erase(it);
}

void PageTiles::remove(int page)
{
    dropStandIns(page);
    dropCover(page);
    const auto tiles = m_tiles.find(page);
    if (tiles == m_tiles.end()) return;
    for (const Tile &tile : std::as_const(*tiles)) release(tile.widget.data());
    m_tiles.erase(tiles);
}

void PageTiles::removeOutside(int first, int last)
{
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ) {
        if (it.key() >= first && it.key() <= last) { ++it; continue; }
        for (const Tile &tile : std::as_const(*it)) {
            keep(it.key(), tile);
            release(tile.widget.data());
        }
        it = m_tiles.erase(it);
    }
    const QList<int> pages = m_standIns.keys();
    for (int page : std::as_const(pages))
        if (page < first || page > last) dropStandIns(page);
    for (int page : m_covers.keys())
        if (page < first || page > last) dropCover(page);
}

void PageTiles::clear()
{
    for (const auto &tiles : std::as_const(m_tiles))
        for (const Tile &tile : tiles) delete tile.widget.data();
    m_tiles.clear();
    const QList<int> pages = m_standIns.keys();
    for (int page : pages) dropStandIns(page);
    for (int page : m_covers.keys()) dropCover(page);
    m_kept.clear();
    m_keptOrder.clear();
    m_keptSize = 0;
}
