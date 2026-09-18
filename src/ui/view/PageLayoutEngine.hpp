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
#  include "engine/edit/EditSession.hpp"
class PdfRenderer;
#endif

/// Builds and maintains the page widgets — single column and grid.
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

    void setZoom(int percent);
    int  zoom() const { return m_zoom; }

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

    void buildGridItems();

    void relayoutGrid(int availableWidth);
    void clearGrid();
    bool gridActive() const { return m_gridActive; }

Q_SIGNALS:

    void layoutChanged();

    void pageActivated(int page);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:

    void resizePages();

    void renderPending();

    void renderNow(int page);
    void scheduleRender(int delayMs);

    std::pair<int, int> visibleRange() const;

    QWidget     *m_canvas     { nullptr };
    QVBoxLayout *m_layout     { nullptr };
    QWidget     *m_gridCanvas { nullptr };

    QList<QLabel *> m_pageLabels;

    /// Caches a page render and its zoom level.
    struct Rendered { QPixmap pixmap; int zoom = 0; };
    QHash<int, Rendered> m_rendered;

    QRect   m_visibleRect;
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
    void renderNextThumbnail(int generation, int index, int attempt = 0);

    int m_pageCount { 0 };
    int m_zoom      { 100 };

#ifdef HAVE_PDF_RENDERING
    PdfRenderer *m_renderer { nullptr };
    EditSession *m_session  { nullptr };
#endif
};
