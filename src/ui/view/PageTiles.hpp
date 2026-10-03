#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

/// Sharp renders, in a fixed grid, of the visible part of pages too large to render whole.
class PageTiles
{
public:
    static constexpr int kSize = 512;

    ~PageTiles();

    static QList<QRect> cells(const QRect &pixels, const QSize &pageSize);

    void show(QLabel *pageLabel, int page, const QRect &cell, const QImage &image,
              qreal dpr, int zoom, int version, const QRect &source = {});
    void patch(int page, const QRect &pixels, const QImage &image, int version,
               const QImage &before = {});

    void capture(int page, const QRect &pixels, QImage &into) const;

    bool has(int page, const QRect &cell, int zoom, int version) const;
    bool covers(int page, const QRect &pixels, const QSize &pageSize, int zoom,
                int version) const;
    bool current(int page, int zoom, int version) const;

    void keepOnly(int page, const QRect &pixels);
    void rescale(qreal ratio);
    void adopt(QLabel *pageLabel, int page, const QRect &area, const QImage &image, qreal dpr,
               int zoom, int version, const QSize &pageSize);
    void cover(QLabel *pageLabel, int page, const QImage &image, const QRect &pixels, qreal dpr);
    void dropCover(int page);
    bool hasCover(int page) const { return m_covers.value(page) != nullptr; }
    void dropStandIns(int page);
    void setKeptBytes(qint64 bytes) { m_keptLimit = bytes; }
    bool restore(QLabel *pageLabel, int page, const QRect &cell, qreal dpr, int zoom, int version);
    bool keptCovers(int page, int zoom, int version, const QRect &pixels, const QSize &pageSize) const;
    void remove(int page);
    void removeOutside(int first, int last);
    void clear();

private:
    struct Tile {
        QPointer<QWidget> widget;
        QRect   pixels;
        int     zoom    { 0 };
        int     version { 0 };
        QRect   partial;
    };
    static quint64 key(const QRect &cell);
    static quint64 keptKey(int page, int zoom, const QRect &cell);
    void keep(int page, const Tile &tile);

    /// A tile no longer on the page, kept so it comes back at once when it is needed again.
    struct Kept { QImage image; QRect source; QRect pixels; int version = 0; };

    QWidget *take(QLabel *pageLabel);
    void     release(QWidget *widget);

    QHash<int, QHash<quint64, Tile>> m_tiles;
    QHash<int, QList<QPointer<QWidget>>> m_standIns;
    QHash<int, QPointer<QWidget>> m_covers;
    QHash<quint64, Kept> m_kept;
    QList<quint64>       m_keptOrder;
    qint64               m_keptSize  { 0 };
    qint64               m_keptLimit { 0 };
    QList<QPointer<QWidget>> m_spare;
};
