#include "PageLayoutEngine.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/edit/EditSession.hpp"
#  include "engine/view/PdfRenderer.hpp"
#endif

#include <QCoreApplication>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <limits>

namespace GridConst {
    constexpr int RENDER_W  = 400;  // internal render quality (px wide)
    constexpr int MIN_CARD_W = 180; // minimum card width for column count calculation
    constexpr int LABEL_H   = 24;
    constexpr int V_PAD     = 10;
    constexpr int COL_GAP   = 16;
    constexpr int ROW_GAP   = 20;
    constexpr int MARGIN    = 24;
}

namespace {
    // A wheel gesture arrives as a burst of ticks. Rendering after each one
    // means the user waits for renders of zoom levels that were already left
    // behind — collect the burst and render the level it ended on.
    constexpr int kZoomRenderDelayMs   = 110;
    // Scrolling only ever exposes pages, so it can react much faster.
    constexpr int kScrollRenderDelayMs = 30;
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
}

#ifdef HAVE_PDF_RENDERING
void PageLayoutEngine::setSource(PdfRenderer *renderer, EditSession *session)
{
    m_renderer = renderer;
    m_session  = session;
}
#endif

// ── Single-column pages ───────────────────────────────────────────────────────

void PageLayoutEngine::clearPages()
{
    m_renderTimer->stop();
    m_rendered.clear();
    m_blank = {};
    for (QLabel *lbl : m_pageLabels) {
        m_layout->removeWidget(lbl);
        delete lbl;
    }
    m_pageLabels.clear();
}

void PageLayoutEngine::buildPages()
{
    clearPages();

    for (int i = 0; i < m_pageCount; ++i) {
        auto *lbl = new QLabel(m_canvas);
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
    }

    // A freshly built document is shown from the top, whatever the previous
    // one was scrolled to — keep the window there so the first render pass
    // covers the pages the user is about to see.
    m_visibleRect.moveTo(0, 0);
    rerenderAll();
}

void PageLayoutEngine::setZoom(int percent)
{
    if (percent <= 0 || percent == m_zoom) return;
    m_zoom = percent;
    resizePages();
    scheduleRender(kZoomRenderDelayMs);
}

void PageLayoutEngine::setVisibleRect(const QRect &canvasRect)
{
    if (canvasRect == m_visibleRect) return;
    m_visibleRect = canvasRect;
    scheduleRender(kScrollRenderDelayMs);
}

void PageLayoutEngine::scheduleRender(int delayMs)
{
    if (m_pageLabels.isEmpty()) return;
    // A pass that is already due sooner keeps its deadline: a scroll during a
    // zoom burst must not be pushed back behind the zoom's longer delay.
    if (m_renderTimer->isActive() && m_renderTimer->remainingTime() <= delayMs)
        return;
    m_renderTimer->start(delayMs);
}

void PageLayoutEngine::resizePages()
{
#ifdef HAVE_PDF_RENDERING
    if (!m_renderer) return;
    for (int i = 0; i < m_pageLabels.size(); ++i) {
        const QSize sz = m_renderer->pageDisplaySize(i, m_zoom);
        if (sz.isEmpty()) continue;
        QLabel *lbl = m_pageLabels[i];

        // Stand-in until the real render lands. It is always scaled from the
        // last REAL render, never from a previous stand-in, so a burst of
        // wheel ticks does not blur the page a little more every time.
        const auto it = m_rendered.constFind(i);
        if (it != m_rendered.cend() && !it->pixmap.isNull()) {
            const qreal dpr = it->pixmap.devicePixelRatio();
            QPixmap preview = it->pixmap.scaled((QSizeF(sz) * dpr).toSize(),
                                                Qt::IgnoreAspectRatio,
                                                Qt::FastTransformation);
            preview.setDevicePixelRatio(dpr);
            lbl->setPixmap(preview);
        }
        lbl->setFixedSize(sz);
    }
    // The page positions are read right after this (scroll anchoring, editor
    // placement), and Qt would only update them once the event loop runs.
    if (m_layout) m_layout->activate();
#endif
    Q_EMIT layoutChanged();
}

std::pair<int, int> PageLayoutEngine::visibleRange() const
{
    if (m_pageLabels.isEmpty()) return { 0, -1 };
    if (m_visibleRect.isEmpty()) return { 0, 0 };   // not measured yet: first page

    // Half a screen of slack on each side so a short scroll never lands on a
    // page that still has to be rendered.
    const int slack = m_visibleRect.height() / 2;
    const QRect window = m_visibleRect.adjusted(0, -slack, 0, slack);

    int first = -1, last = -1;
    for (int i = 0; i < m_pageLabels.size(); ++i) {
        const QRect g = m_pageLabels[i]->geometry();
        if (g.bottom() < window.top() || g.top() > window.bottom()) continue;
        if (first < 0) first = i;
        last = i;
    }
    if (first >= 0) return { first, last };

    // Nothing intersects (page geometry not laid out yet, scrolled past the
    // end): fall back to the page closest to the middle of the window.
    const int center = window.center().y();
    int best = 0, bestDist = std::numeric_limits<int>::max();
    for (int i = 0; i < m_pageLabels.size(); ++i) {
        const int d = qAbs(m_pageLabels[i]->geometry().center().y() - center);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return { best, best };
}

void PageLayoutEngine::renderPending()
{
#ifdef HAVE_PDF_RENDERING
    m_renderTimer->stop();
    if (!m_renderer || m_pageLabels.isEmpty()) return;

    const auto [first, last] = visibleRange();
    if (last < first) return;

    bool changed = false;

    // Free what left the window BEFORE rendering what entered it, so the peak
    // stays at a couple of pages even at 300 %.
    for (auto it = m_rendered.begin(); it != m_rendered.end(); ) {
        if (it.key() < first || it.key() > last) {
            if (QLabel *lbl = m_pageLabels.value(it.key(), nullptr))
                lbl->setPixmap(QPixmap());   // falls back to the white page sheet
            it = m_rendered.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    for (int i = first; i <= last; ++i) {
        const auto it = m_rendered.constFind(i);
        if (it != m_rendered.cend() && it->zoom == m_zoom) continue;
        renderNow(i);
        changed = true;
    }
    if (changed) Q_EMIT layoutChanged();
#endif
}

void PageLayoutEngine::renderNow(int page)
{
#ifdef HAVE_PDF_RENDERING
    QLabel *lbl = m_pageLabels.value(page, nullptr);
    if (!m_renderer || !lbl) return;

    const qreal dpr   = m_canvas->devicePixelRatioF();
    const qreal scale = PdfRenderer::screenScale(m_zoom);
    QImage img = m_renderer->renderPage(page, scale * dpr);
    if (img.isNull()) {
        // A render that failed (a page Poppler chokes on, or no memory for the
        // bitmap at high zoom) must not leave the pixmap of the PREVIOUS zoom
        // on the label — it would be drawn clipped inside the new page size
        // and look like a corrupted page.
        lbl->setPixmap(QPixmap());
        m_rendered.remove(page);
        return;
    }

    // NOTE: paint in RAW device pixels first, tag the DPR only afterwards — a
    // QPainter on a DPR-tagged image multiplies every coordinate by dpr and
    // would displace the erase rects and all edit painting on scaled displays
    // (fractional scaling!).
    const qreal s = scale * dpr;
    if (m_blank.page == page) {
        // Erase the original text BEFORE applying session edits so it is
        // hidden even on the first click. Two safety properties:
        //   • only the tight glyph rects are touched (graphics survive)
        //   • the background is reconstructed from real surrounding pixels,
        //     never a guessed color
        const qreal pad = qMax(1.0, 2.5 * s);
        const QList<QRectF> areas = m_blank.rects.isEmpty()
                                        ? QList<QRectF>{ m_blank.bounds }
                                        : m_blank.rects;
        QList<QRect> rects;
        rects.reserve(areas.size());
        for (const QRectF &a : areas) {
            const QRectF px(a.topLeft() * s, a.size() * s);
            rects.append(px.adjusted(-pad, -pad, pad, pad).toAlignedRect());
        }
        // One call for the whole block: the reconstruction samples outside
        // the block, never between its tightly stacked lines.
        QPainter p(&img);
        EditSession::paintBackgroundPatch(p, img, rects);
    }
    if (m_session) m_session->applyToImage(page, img, s);
    img.setDevicePixelRatio(dpr);

    QPixmap pm = QPixmap::fromImage(std::move(img));
    // pageDisplaySize() sized the widget for the same zoom, but on a fractional
    // display scale the two roundings can still end up a pixel apart. Keep the
    // widget exactly as large as what it shows — QLabel centres a pixmap that
    // does not fit and clips it.
    const QSize logical = (QSizeF(pm.size()) / pm.devicePixelRatio()).toSize();
    if (!logical.isEmpty() && logical != lbl->size()) lbl->setFixedSize(logical);
    lbl->setPixmap(pm);
    m_rendered.insert(page, { pm, m_zoom });
#else
    Q_UNUSED(page)
#endif
}

void PageLayoutEngine::rerenderAll()
{
    // Everything on screen is stale (new document, saved file, undo). Dropping
    // the pixmaps instead of only the cache also frees the pages that are
    // currently off screen; the visible ones are painted again right below,
    // before anything reaches the screen.
    for (QLabel *lbl : m_pageLabels) lbl->setPixmap(QPixmap());
    m_rendered.clear();
    resizePages();
    renderPending();
}

void PageLayoutEngine::rerenderPage(int page)
{
    if (page < 0 || page >= m_pageLabels.size()) return;
    if (m_blank.page == page) m_blank = {};   // the edit that owned it is over
    m_rendered.remove(page);
    const auto [first, last] = visibleRange();
    // Off-screen pages have no pixmap to refresh; dropping the cache entry
    // above is enough to make them render fresh when they scroll back in.
    if (page >= first && page <= last) renderNow(page);
}

void PageLayoutEngine::rerenderPageWithBlank(int page, const QRectF &pdfBoundsPts,
                                             const QList<QRectF> &eraseRects)
{
    if (page < 0 || page >= m_pageLabels.size()) return;
    m_blank = { page, pdfBoundsPts, eraseRects };
    m_rendered.remove(page);
    renderNow(page);
}

// ── Grid view ─────────────────────────────────────────────────────────────────

void PageLayoutEngine::clearGrid()
{
    for (const GridItem &item : m_gridItems)
        delete item.card;
    m_gridItems.clear();
    m_gridCardIndex.clear();
    m_gridActive = false;
}

void PageLayoutEngine::buildGridItems()
{
    clearGrid();
    m_gridActive = true;

#ifdef HAVE_PDF_RENDERING
    if (!m_renderer) return;
    const qreal dpr = m_canvas->devicePixelRatioF();

    for (int i = 0; i < m_pageCount; ++i) {
        auto *card = new QFrame(m_gridCanvas);
        card->setObjectName(QStringLiteral("GridCard"));
        card->setCursor(Qt::PointingHandCursor);
        card->installEventFilter(this);

        auto *vl = new QVBoxLayout(card);
        vl->setContentsMargins(GridConst::V_PAD, GridConst::V_PAD,
                               GridConst::V_PAD, GridConst::V_PAD);
        vl->setSpacing(6);

        auto *thumb = new QLabel(card);
        thumb->setObjectName(QStringLiteral("GridThumb"));
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        thumb->setAutoFillBackground(true);
        QPalette pal = thumb->palette();
        pal.setColor(QPalette::Window, Qt::white);
        thumb->setPalette(pal);

        // Translated under the DocumentView context before the extraction —
        // naming it explicitly keeps the existing .ts entries valid.
        auto *lbl = new QLabel(
            QCoreApplication::translate("DocumentView", "Page %1").arg(i + 1), card);
        lbl->setObjectName(QStringLiteral("GridPageLabel"));
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        vl->addWidget(thumb, 1);
        vl->addWidget(lbl, 0);

        // Render at RENDER_W for quality; relayoutGrid scales to actual card width
        QPixmap original;
        const QSize sz100 = m_renderer->pageDisplaySize(i, 100);
        if (sz100.width() > 0) {
            const int   tZoom  = qMax(1, GridConst::RENDER_W * 100 / sz100.width());
            const qreal tScale = PdfRenderer::screenScale(tZoom);
            QImage img = m_renderer->renderPage(i, tScale * dpr);
            if (m_session) m_session->applyToImage(i, img, tScale * dpr);
            img.setDevicePixelRatio(dpr);
            if (!img.isNull())
                original = QPixmap::fromImage(std::move(img));
        }
        thumb->setPixmap(original);

        m_gridCardIndex[card] = i;
        m_gridItems.append({card, thumb, lbl, original});
    }
#endif
}

void PageLayoutEngine::relayoutGrid(int availableWidth)
{
    if (m_gridItems.isEmpty()) return;

    // availableWidth is the scroll area's viewport width, measured by the caller
    // either from resizeEvent (after Qt has already resized the viewport) or from
    // a QueuedConnection (after the event loop processes the new widget install).
    // setWidgetResizable(true) keeps the canvas at exactly viewport width, so this
    // is the authoritative measurement.
    const int availW = qMax(GridConst::MIN_CARD_W + GridConst::MARGIN * 2,
                            availableWidth);

    // Fill the full row: compute column count from MIN_CARD_W, then expand each card.
    const int cols  = qMax(1, (availW - GridConst::MARGIN * 2 + GridConst::COL_GAP)
                               / (GridConst::MIN_CARD_W + GridConst::COL_GAP));
    const int cardW = (availW - GridConst::MARGIN * 2 - (cols - 1) * GridConst::COL_GAP) / cols;
    const int thumbW = qMax(1, cardW - GridConst::V_PAD * 2);

    // Compute thumb height from page 0 aspect ratio
    int thumbH = qRound(thumbW * 1.414);  // A4 portrait fallback
#ifdef HAVE_PDF_RENDERING
    if (m_renderer && m_pageCount > 0) {
        const QSize sz100 = m_renderer->pageDisplaySize(0, 100);
        if (sz100.width() > 0)
            thumbH = qRound(qreal(thumbW) * sz100.height() / sz100.width());
    }
#endif
    const int cardH  = GridConst::V_PAD + thumbH + 6 + GridConst::LABEL_H + GridConst::V_PAD;
    const int rows   = (m_gridItems.size() + cols - 1) / cols;
    const int totalH = GridConst::MARGIN
                       + rows * (cardH + GridConst::ROW_GAP) - GridConst::ROW_GAP
                       + GridConst::MARGIN;

    // setWidgetResizable(true) manages the width; we only control the height so the
    // scroll area can add a vertical scrollbar when the grid is taller than the viewport.
    m_gridCanvas->setMinimumHeight(totalH);

    for (int i = 0; i < m_gridItems.size(); ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const int x   = GridConst::MARGIN + col * (cardW + GridConst::COL_GAP);
        const int y   = GridConst::MARGIN + row * (cardH + GridConst::ROW_GAP);
        m_gridItems[i].card->setGeometry(x, y, cardW, cardH);

        // Scale stored original pixmap to match the current thumb area
        if (!m_gridItems[i].original.isNull())
            m_gridItems[i].thumb->setPixmap(
                m_gridItems[i].original.scaled(thumbW, thumbH,
                    Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

bool PageLayoutEngine::eventFilter(QObject *obj, QEvent *e)
{
    // Grid card click → the view switches to single mode and scrolls there.
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
