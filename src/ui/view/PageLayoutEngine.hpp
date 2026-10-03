#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QRect>
#include <QRectF>

#include <functional>
#include <utility>

QT_BEGIN_NAMESPACE
class QFrame;
class QLabel;
class QTimer;
class QVBoxLayout;
class QWidget;
QT_END_NAMESPACE

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentWorker.hpp"
#  include "engine/edit/EditSession.hpp"
class PdfRenderer;
#endif

#include <QSet>

#include "ui/view/PageTiles.hpp"

/// Builds and maintains the page widgets for single column and grid.
class PageLayoutEngine : public QObject
{
    Q_OBJECT

public:
    PageLayoutEngine(QWidget *canvas, QVBoxLayout *layout, QWidget *gridCanvas,
                     QObject *parent = nullptr);

#ifdef HAVE_PDF_RENDERING
    void setSource(PdfRenderer *renderer, EditSession *session, DocumentWorker *worker);
#endif
    void setPageCount(int count) { m_pageCount = count; }

    void setZoom(int percent);
    void setZoomStep(int step) { m_zoomStep = qMax(1, step); }
    void setEditMode(bool on) { m_editMode = on; }
    int  zoom() const { return m_zoom; }
    bool sharpAt(int percent, const QPoint &canvasAnchor) const;
    void prepareZoom(int percent, const QPoint &canvasAnchor = {});

    void setVisibleRect(const QRect &canvasRect);

    void buildPages();
    void clearPages();
    void rerenderAll();
    void rerenderPage(int page);

    void rerenderPageWithBlank(int page, const QRectF &pdfBoundsPts,
                               const QList<QRectF> &eraseRects);

    void setPreviewEdits(int page, const QList<EditSession::Edit> &edits);

    QLabel *pageLabel(int page) const { return m_pageLabels.value(page, nullptr); }
    int     pageLabelCount()    const { return static_cast<int>(m_pageLabels.size()); }

    bool busy() const { return !m_pending.isEmpty(); }

    void buildGridItems();

    void relayoutGrid(int availableWidth);
    void clearGrid();
    bool gridActive() const { return m_gridActive; }
    void setGridVisibleRect(const QRect &rect);

Q_SIGNALS:

    void layoutChanged();

    void busyChanged(bool busy);

    void zoomPrepared();

    void pageActivated(int page);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    static constexpr int kScrollRenderDelayMs = 30;

    void resizePages();

    void renderPending();

    void renderNow(int page);
    void renderTileNow(int page);
    void refreshPage(int page);
    void refreshSoon(int page);
    void refreshQueued();
    bool patchPage(int page);
    static void patchPixmap(QPixmap &pixmap, const QRect &pixels, const QImage &image);
    void catchUpBase(int page);
    QImage base(int page, const QRect &pixels) const;
    void catchUpBases();
    bool editedOnPage(int page) const;
    void noteChanged(int page, int version, const QRect &pixels, qreal scale);
    QRect changedAt(int page, qreal scale) const;
#ifdef HAVE_PDF_RENDERING
    EditSession stateFor(int page) const;
    qreal deviceScale() const;
    bool  tiled(int page) const;
    bool  tiledAt(int page, qreal scale) const;
    qreal baseScale(int page) const;
    QRect wantedTile(int page, bool withMargin) const;
    QSize fullPixels(int page) const;
    QList<QRect> missingCells(int page, bool withMargin) const;
    void restoreTiles(int page, const QRect &pixels);
    bool  sharp(int page) const;
    bool  blank(int page) const;
    void  showBase(int page, QImage image);
#endif
    void scheduleRender(int delayMs);
    void showRendered(int page, int zoom, int version, const QRect &area, const QImage &image,
                      int milliseconds);
    bool showPlaceholder(int page);
    void keepPreview(int page, const QImage &image);
    void evictRendered(int first, int last);
    void setPending(QSet<int> pages);
    int  bumpVersion(int page);

    std::pair<int, int> visibleRange(int slackDivisor = 2) const;

    QWidget     *m_canvas     { nullptr };
    QVBoxLayout *m_layout     { nullptr };
    QWidget     *m_gridCanvas { nullptr };

    QList<QLabel *> m_pageLabels;

    /// Caches a page render with the zoom and page version it was made for.
    struct Rendered { QPixmap pixmap; int zoom = 0; int version = 0; };
    QHash<int, Rendered> m_rendered;
    QHash<int, QPixmap>  m_previews;
    QList<int>           m_pageVersions;

    /// The part of a page's render that differs from the unedited page.
    struct Changed { int version = 0; QRect area; };
    QHash<int, Changed>  m_changed;
    QHash<int, QRectF>   m_changedPts;

#ifdef HAVE_PDF_RENDERING
    /// The last exactly rendered state of a page, its pixels over the edited
    /// area and a partial render of it over the same area.
    struct Anchor { qreal scale = 0; EditSession state; QRect area; QImage exact; QImage render; };
    QHash<int, Anchor>   m_anchors;

    /// The visible part of a page rendered at the zoom levels one wheel step
    /// away, shown unscaled the moment the zoom reaches them.
    struct Level { int version = -1; QRect pixels; QImage image; };
    QHash<quint64, Level> m_levels;
    QHash<quint64, Level> m_levelAsked;
    QRect levelArea(int page, int zoom) const;
    QRect pagePixels(int page, const QRect &canvasRect, qreal scale) const;
    QRect zoomView(int percent, const QPoint &canvasAnchor, int margin = 24) const;
    QPoint pointerAnchor() const;
    bool  adoptLevel(int page);
    QList<int> zoomPath() const;
    QRect wantedArea(int page, const QRect &canvasRect, qreal scale) const;

    // Renders of a neighbouring zoom level carry this plus their zoom.
    static constexpr int kLevelTag = 100000;
    // Renders for the grid of thumbnails carry this and the grid's generation.
    static constexpr int kGridTag = -2;
    static quint64 levelKey(int page, int zoom) { return (quint64(page) << 16) | quint64(zoom); }

    struct ZoomRenders {
        QList<DocumentWorker::Render> wanted, levelsNear, levelsFar;
    };
    ZoomRenders planZoomRenders(int first, int last, int shownFirst, int shownLast,
                                bool viewDone, bool idle,
                                const std::function<DocumentWorker::Render(int)> &request);
    bool keepZoomRender(int page, int zoom, int version, const QRect &area, const QImage &image);
    QHash<int, int>      m_wholeRenderMs;
#endif
    PageTiles            m_tiles;

    /// Where a tiled page's capped base still shows an older state.
    struct Stale { QRect area; };
    QHash<int, Stale>    m_staleBase;
    QTimer              *m_staleBaseTimer { nullptr };
    QSet<int>            m_inexact;
    QTimer              *m_settleTimer { nullptr };
    QTimer              *m_refreshTimer { nullptr };
    QSet<int>            m_refreshQueue;
    bool                 m_settling { false };
    QHash<int, QList<QRect>> m_settleLeft;
    void settled(int page, const QRect &part);
    int                  m_versionCounter { 0 };
    QSet<int>            m_pending;

    QRect   m_visibleRect;
    QPoint  m_scrollDirection;
    QTimer *m_renderTimer { nullptr };

    /// Stores the persistent erase area for one page.
    struct Blank { int page = -1; QRectF bounds; QList<QRectF> rects; };
    Blank m_blank;

    struct Preview { int page = -1; QList<EditSession::Edit> edits; };
    Preview m_preview;

    struct GridItem { QFrame *card; QLabel *thumb; QLabel *label; QPixmap original; };
    QList<GridItem>       m_gridItems;
    QHash<QObject *, int> m_gridCardIndex;
    bool                  m_gridActive { false };

    int                   m_gridGeneration { 0 };
    QRect m_gridVisible;
    void renderGrid();
    void showThumbnail(int index, int generation, const QImage &image);

    int m_pageCount { 0 };
    int m_zoom      { 100 };
    int    m_zoomStep   { 10 };
    int    m_zoomDirection { 1 };
    bool   m_levelShown { false };
    bool   m_editMode   { false };
    int    m_zoomWanted { 0 };
    QPoint m_zoomAnchor;

#ifdef HAVE_PDF_RENDERING
    PdfRenderer    *m_renderer { nullptr };
    EditSession    *m_session  { nullptr };
    DocumentWorker *m_worker   { nullptr };
#endif
};
