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
#include <QVBoxLayout>
#include <QWidget>

namespace GridConst {
    constexpr int RENDER_W  = 400;  // internal render quality (px wide)
    constexpr int MIN_CARD_W = 180; // minimum card width for column count calculation
    constexpr int LABEL_H   = 24;
    constexpr int V_PAD     = 10;
    constexpr int COL_GAP   = 16;
    constexpr int ROW_GAP   = 20;
    constexpr int MARGIN    = 24;
}

PageLayoutEngine::PageLayoutEngine(QWidget *canvas, QVBoxLayout *layout,
                                   QWidget *gridCanvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
    , m_layout(layout)
    , m_gridCanvas(gridCanvas)
{}

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

    rerenderAll();
}

void PageLayoutEngine::rerenderAll()
{
#ifdef HAVE_PDF_RENDERING
    if (!m_renderer) return;
    const qreal dpr   = m_canvas->devicePixelRatioF();
    const qreal scale = PdfRenderer::screenScale(m_zoom);
    for (int i = 0; i < m_pageLabels.size(); ++i) {
        const QSize sz = m_renderer->pageDisplaySize(i, m_zoom);
        m_pageLabels[i]->setFixedSize(sz);
        QImage img = m_renderer->renderPage(i, scale * dpr);
        // Paint in RAW device pixels first; tag the DPR only afterwards —
        // a QPainter on a DPR-tagged image multiplies every coordinate by
        // dpr and would displace all edit painting on scaled displays.
        if (m_session) m_session->applyToImage(i, img, scale * dpr);
        img.setDevicePixelRatio(dpr);
        if (!img.isNull())
            m_pageLabels[i]->setPixmap(QPixmap::fromImage(std::move(img)));
    }
#endif
    Q_EMIT layoutChanged();
}

void PageLayoutEngine::rerenderPage(int page)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_renderer || page < 0 || page >= m_pageLabels.size()) return;
    const qreal dpr   = m_canvas->devicePixelRatioF();
    const qreal scale = PdfRenderer::screenScale(m_zoom);
    QImage img = m_renderer->renderPage(page, scale * dpr);
    // Paint first, tag DPR afterwards (see rerenderAll).
    if (m_session) m_session->applyToImage(page, img, scale * dpr);
    img.setDevicePixelRatio(dpr);
    if (!img.isNull())
        m_pageLabels[page]->setPixmap(QPixmap::fromImage(std::move(img)));
#else
    Q_UNUSED(page)
#endif
}

void PageLayoutEngine::rerenderPageWithBlank(int page, const QRectF &pdfBoundsPts,
                                             const QList<QRectF> &eraseRects)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_renderer || page < 0 || page >= m_pageLabels.size()) return;
    const qreal dpr   = m_canvas->devicePixelRatioF();
    const qreal scale = PdfRenderer::screenScale(m_zoom);
    QImage img = m_renderer->renderPage(page, scale * dpr);
    // NOTE: the DPR is tagged only AFTER all painting — a QPainter on a
    // DPR-tagged image multiplies every coordinate by dpr and would displace
    // the erase rects on scaled displays (fractional scaling!).
    {
        // Erase the original text BEFORE applying session edits so it is
        // hidden even on the first click. Two safety properties:
        //   • only the tight glyph rects are touched (graphics survive)
        //   • the background is reconstructed from real surrounding pixels,
        //     never a guessed color
        const qreal s   = scale * dpr;
        const qreal pad = qMax(1.0, 2.5 * s);
        const QList<QRectF> areas = eraseRects.isEmpty()
                                        ? QList<QRectF>{ pdfBoundsPts }
                                        : eraseRects;
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
    if (m_session) m_session->applyToImage(page, img, scale * dpr);
    img.setDevicePixelRatio(dpr);
    if (!img.isNull())
        m_pageLabels[page]->setPixmap(QPixmap::fromImage(std::move(img)));
#else
    Q_UNUSED(page) Q_UNUSED(pdfBoundsPts) Q_UNUSED(eraseRects)
#endif
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
