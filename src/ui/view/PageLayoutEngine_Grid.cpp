#include "ui/view/PageLayoutEngine.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentWorker.hpp"
#  include "engine/edit/EditSession.hpp"
#  include "engine/render/PdfRenderer.hpp"
#endif

#include <QCoreApplication>
#include <QFrame>
#include <QLabel>
#include <QPalette>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

namespace GridConst {
    constexpr int RENDER_W  = 400;
    constexpr int MIN_CARD_W = 180;
    constexpr int LABEL_H   = 24;
    constexpr int V_PAD     = 10;
    constexpr int COL_GAP   = 16;
    constexpr int ROW_GAP   = 20;
    constexpr int MARGIN    = 24;
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

    if (!m_gridItems.isEmpty()) scheduleRender(0);
#endif
}

void PageLayoutEngine::setGridVisibleRect(const QRect &rect)
{
    if (rect == m_gridVisible) return;
    m_gridVisible = rect;
    if (m_gridActive) scheduleRender(kScrollRenderDelayMs);
}

void PageLayoutEngine::renderGrid()
{
#ifdef HAVE_PDF_RENDERING
    if (!m_worker || !m_renderer) return;
    // Every thumbnail still missing, those in view and just below first.
    const QRect near = m_gridVisible.adjusted(0, 0, 0, m_gridVisible.height());
    QList<DocumentWorker::Render> inView, later;
    std::shared_ptr<const EditSession> snapshot;
    const qreal dpr = m_canvas->devicePixelRatioF();
    for (int index = 0; index < m_gridItems.size(); ++index) {
        if (!m_gridItems[index].original.isNull()) continue;
        const QSize sz100 = m_renderer->pageDisplaySize(index, 100);
        if (sz100.width() <= 0) continue;
        DocumentWorker::Render render;
        render.page    = index;
        render.zoom    = kGridTag;
        render.version = m_gridGeneration;
        render.scale   = PdfRenderer::screenScale(qMax(1, GridConst::RENDER_W * 100 / sz100.width())) * dpr;
        if (editedOnPage(index)) {
            if (!snapshot) snapshot = std::make_shared<EditSession>(*m_session);
            render.session = snapshot;
        }
        (m_gridItems[index].card->geometry().intersects(near) ? inView : later).append(render);
    }
    m_worker->setRenders(inView + later);
#endif
}

void PageLayoutEngine::showThumbnail(int index, int generation, const QImage &image)
{
    if (generation != m_gridGeneration || index < 0 || index >= m_gridItems.size()) return;
    GridItem &item = m_gridItems[index];
    QImage img = image;
    img.setDevicePixelRatio(m_canvas->devicePixelRatioF());
    item.original = QPixmap::fromImage(std::move(img));
    const QSize area = item.thumb ? item.thumb->size() : QSize();
    if (item.thumb)
        item.thumb->setPixmap(area.width() >= 20 && area.height() >= 20
            ? item.original.scaled(area, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            : item.original);
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
    scheduleRender(0);
}
