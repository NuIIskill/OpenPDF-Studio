#include "ui/view/PageLayoutEngine.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/edit/EditSession.hpp"
#  include "engine/render/PdfRenderer.hpp"
#endif

#include <QCoreApplication>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <limits>

namespace GridConst {
    constexpr int RENDER_W  = 400;
    constexpr int MIN_CARD_W = 180;
    constexpr int LABEL_H   = 24;
    constexpr int V_PAD     = 10;
    constexpr int COL_GAP   = 16;
    constexpr int ROW_GAP   = 20;
    constexpr int MARGIN    = 24;
}

namespace {

    constexpr int kZoomRenderDelayMs   = 110;

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

    if (m_layout) m_layout->activate();
#endif
    Q_EMIT layoutChanged();
}

std::pair<int, int> PageLayoutEngine::visibleRange() const
{
    if (m_pageLabels.isEmpty()) return { 0, -1 };
    if (m_visibleRect.isEmpty()) return { 0, 0 };

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

    for (auto it = m_rendered.begin(); it != m_rendered.end(); ) {
        if (it.key() < first || it.key() > last) {
            if (QLabel *lbl = m_pageLabels.value(it.key(), nullptr))
                lbl->setPixmap(QPixmap());
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

    EditSession preview;
    const EditSession *use = m_session;
    if (m_session && (m_blank.page == page || m_preview.page == page)) {
        preview = *m_session;
        if (m_blank.page == page) {
            EditSession::Edit blank;
            blank.page       = page;
            blank.pdfBounds  = m_blank.bounds;
            blank.eraseRects = m_blank.rects;
            preview.addEdit(std::move(blank));
        }
        if (m_preview.page == page)
            for (const EditSession::Edit &e : m_preview.edits)
                preview.addEdit(e);
        use = &preview;
    }
    QImage img = m_renderer->renderPage(page, scale * dpr, use);
    if (img.isNull()) {

        lbl->setPixmap(QPixmap());
        m_rendered.remove(page);
        return;
    }

    img.setDevicePixelRatio(dpr);

    QPixmap pm = QPixmap::fromImage(std::move(img));

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

    for (QLabel *lbl : m_pageLabels) lbl->setPixmap(QPixmap());
    m_rendered.clear();
    resizePages();
    renderPending();
}

void PageLayoutEngine::rerenderPage(int page)
{
    if (page < 0 || page >= m_pageLabels.size()) return;
    if (m_blank.page == page) m_blank = {};
    if (m_preview.page == page) m_preview = {};
    m_rendered.remove(page);
    const auto [first, last] = visibleRange();

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

void PageLayoutEngine::setPreviewEdits(int page, const QList<EditSession::Edit> &edits)
{
    if (m_preview.page == page && m_preview.edits == edits) return;
    const int was = m_preview.page;
    m_preview = { page, edits };
    if (was >= 0 && was != page) { m_rendered.remove(was); renderNow(was); }
    if (page >= 0) { m_rendered.remove(page); renderNow(page); }
}

void PageLayoutEngine::clearGrid()
{
    for (const GridItem &item : m_gridItems)
        delete item.card;
    m_gridItems.clear();
    m_gridCardIndex.clear();
    m_gridActive = false;
    ++m_gridGeneration;
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

        auto *lbl = new QLabel(
            QCoreApplication::translate("DocumentView", "Page %1").arg(i + 1), card);
        lbl->setObjectName(QStringLiteral("GridPageLabel"));
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        vl->addWidget(thumb, 1);
        vl->addWidget(lbl, 0);

        m_gridCardIndex[card] = i;
        m_gridItems.append({card, thumb, lbl, QPixmap()});
    }

    if (!m_gridItems.isEmpty()) {
        const int generation = m_gridGeneration;
        QMetaObject::invokeMethod(this, [this, generation]() {
            renderNextThumbnail(generation, 0);
        }, Qt::QueuedConnection);
    }
#endif
}

void PageLayoutEngine::renderNextThumbnail(int generation, int index, int attempt)
{
#ifdef HAVE_PDF_RENDERING

    if (generation != m_gridGeneration || !m_renderer) return;
    if (index < 0 || index >= m_gridItems.size()) return;

    GridItem &item = m_gridItems[index];

    const QSize area = item.thumb ? item.thumb->size() : QSize();
    if ((area.width() < 20 || area.height() < 20) && attempt < 50) {
        QMetaObject::invokeMethod(this, [this, generation, index, attempt]() {
            renderNextThumbnail(generation, index, attempt + 1);
        }, Qt::QueuedConnection);
        return;
    }

    const qreal dpr = m_canvas->devicePixelRatioF();

    const QSize sz100 = m_renderer->pageDisplaySize(index, 100);
    if (sz100.width() > 0) {
        const int   tZoom  = qMax(1, GridConst::RENDER_W * 100 / sz100.width());
        const qreal tScale = PdfRenderer::screenScale(tZoom);
        QImage img = m_renderer->renderPage(index, tScale * dpr, m_session);
        img.setDevicePixelRatio(dpr);
        if (!img.isNull()) item.original = QPixmap::fromImage(std::move(img));
    }

    if (item.thumb && !item.original.isNull()) {
        item.thumb->setPixmap(area.isValid() && area.width() >= 20
            ? item.original.scaled(area, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation)
            : item.original);
    }

    QMetaObject::invokeMethod(this, [this, generation, index]() {
        renderNextThumbnail(generation, index + 1);
    }, Qt::QueuedConnection);
#else
    Q_UNUSED(generation) Q_UNUSED(index)
#endif
}

void PageLayoutEngine::relayoutGrid(int availableWidth)
{
    if (m_gridItems.isEmpty()) return;

    const int availW = qMax(GridConst::MIN_CARD_W + GridConst::MARGIN * 2,
                            availableWidth);

    const int cols  = qMax(1, (availW - GridConst::MARGIN * 2 + GridConst::COL_GAP)
                               / (GridConst::MIN_CARD_W + GridConst::COL_GAP));
    const int cardW = (availW - GridConst::MARGIN * 2 - (cols - 1) * GridConst::COL_GAP) / cols;
    const int thumbW = qMax(1, cardW - GridConst::V_PAD * 2);

    int thumbH = qRound(thumbW * 1.414);
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

    m_gridCanvas->setMinimumHeight(totalH);

    for (int i = 0; i < m_gridItems.size(); ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const int x   = GridConst::MARGIN + col * (cardW + GridConst::COL_GAP);
        const int y   = GridConst::MARGIN + row * (cardH + GridConst::ROW_GAP);
        m_gridItems[i].card->setGeometry(x, y, cardW, cardH);

        if (!m_gridItems[i].original.isNull())
            m_gridItems[i].thumb->setPixmap(
                m_gridItems[i].original.scaled(thumbW, thumbH,
                    Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

bool PageLayoutEngine::eventFilter(QObject *obj, QEvent *e)
{

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
