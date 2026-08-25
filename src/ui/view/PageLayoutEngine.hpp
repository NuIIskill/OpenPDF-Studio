#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QRect>
#include <QRectF>

#include <utility>

QT_BEGIN_NAMESPACE
class QFrame;
class QLabel;
class QTimer;
class QVBoxLayout;
class QWidget;
QT_END_NAMESPACE

#ifdef HAVE_PDF_RENDERING
class EditSession;
class PdfRenderer;
#endif

// Builds and maintains the page widgets — single column and grid.
//
// Knows the renderer and the session, but no mouse interaction: everything
// that needs the QScrollArea itself (swapping the viewport widget, scrolling,
// the viewport width) stays in DocumentView and is passed in.
class PageLayoutEngine : public QObject
{
    Q_OBJECT

public:
    PageLayoutEngine(QWidget *canvas, QVBoxLayout *layout, QWidget *gridCanvas,
                     QObject *parent = nullptr);

#ifdef HAVE_PDF_RENDERING
    void setSource(PdfRenderer *renderer, EditSession *session);
#endif
    void setPageCount(int count) { m_pageCount = count; }
    // Resizes the page widgets to the new zoom right away and shows the pages
    // that are already rendered scaled as a stand-in; the real render of the
    // visible pages follows shortly after (see scheduleRender). Re-rendering
    // every page on every wheel tick is what made zooming freeze the view.
    void setZoom(int percent);
    int  zoom() const { return m_zoom; }

    // The part of the canvas the user can actually see, in canvas coordinates.
    // Only pages inside it (plus half a screen of slack) hold a rendered
    // pixmap — at 300 % one A4 page is ~30 MB, so keeping all of them ran the
    // process out of memory and left pages half-drawn.
    void setVisibleRect(const QRect &canvasRect);

    // ── Single-column pages ───────────────────────────────────────────────────
    void buildPages();
    void clearPages();
    void rerenderAll();
    void rerenderPage(int page);
    // Re-renders the page normally then paints a white blank over pdfBoundsPts.
    // Called when starting an edit so the original text disappears from view —
    // the editor widget sits over a clean white area instead of over the old
    // text. eraseRects are the tight glyph rects; empty falls back to bounds.
    //
    // The blank sticks to the page: every later render of it (zoom, scrolling
    // back to it) paints it again, until rerenderPage() clears it. Otherwise a
    // zoom while the editor is open would bring the original text back.
    void rerenderPageWithBlank(int page, const QRectF &pdfBoundsPts,
                               const QList<QRectF> &eraseRects);

    QLabel *pageLabel(int page) const { return m_pageLabels.value(page, nullptr); }
    int     pageLabelCount()    const { return static_cast<int>(m_pageLabels.size()); }

    // ── Grid ──────────────────────────────────────────────────────────────────
    void buildGridItems();
    // availableWidth is the scroll area's viewport width — only the caller can
    // measure it reliably.
    void relayoutGrid(int availableWidth);
    void clearGrid();
    bool gridActive() const { return m_gridActive; }

Q_SIGNALS:
    // Page geometry changed — overlays anchored to pages have to follow.
    void layoutChanged();
    // A grid card was clicked.
    void pageActivated(int page);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    // Sizes every page widget for the current zoom and, where a rendered
    // pixmap exists, shows it scaled until the real render replaces it.
    void resizePages();
    // Renders the pages inside the visible window that are not up to date and
    // frees the pixmaps of the pages that left it.
    void renderPending();
    // Real render of one page, including the sticky blank and session edits.
    void renderNow(int page);
    void scheduleRender(int delayMs);
    // First/last page index inside the visible window; {0, -1} when empty.
    std::pair<int, int> visibleRange() const;

    QWidget     *m_canvas     { nullptr };
    QVBoxLayout *m_layout     { nullptr };
    QWidget     *m_gridCanvas { nullptr };

    QList<QLabel *> m_pageLabels;

    // Last real render per page — kept as the source for the scaled stand-in
    // during zooming, so repeated wheel ticks never scale an already scaled
    // pixmap. Bounded to the visible window by renderPending().
    struct Rendered { QPixmap pixmap; int zoom = 0; };
    QHash<int, Rendered> m_rendered;

    QRect   m_visibleRect;              // canvas coords; empty = not measured yet
    QTimer *m_renderTimer { nullptr };

    // Erase patch that survives re-renders of its page (see rerenderPageWithBlank).
    struct Blank { int page = -1; QRectF bounds; QList<QRectF> rects; };
    Blank m_blank;

    struct GridItem { QFrame *card; QLabel *thumb; QLabel *label; QPixmap original; };
    QList<GridItem>       m_gridItems;
    QHash<QObject *, int> m_gridCardIndex;
    bool                  m_gridActive { false };
    /// Thumbnails are rendered one per turn of the event loop, so building a
    /// grid does not hold the whole program still. The counter tells a step
    /// that its grid has been thrown away since it was scheduled.
    int                   m_gridGeneration { 0 };
    void renderNextThumbnail(int generation, int index, int attempt = 0);

    int m_pageCount { 0 };
    int m_zoom      { 100 };

#ifdef HAVE_PDF_RENDERING
    PdfRenderer *m_renderer { nullptr };
    EditSession *m_session  { nullptr };
#endif
};
