#include "ui/view/ZoomController.hpp"

#include "ui/view/PageCanvas.hpp"
#include "ui/view/PageLayoutEngine.hpp"

#include <QCoreApplication>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

ZoomController::ZoomController(QScrollArea *area, PageCanvas *canvas,
                               QVBoxLayout *canvasLayout, PageLayoutEngine *engine,
                               QObject *parent)
    : QObject(parent)
    , m_area(area)
    , m_canvas(canvas)
    , m_layout(canvasLayout)
    , m_engine(engine)
{
}

void ZoomController::setZoom(int percent)
{
    QWidget *vp = m_area->viewport();
    applyZoom(percent, QPoint(vp->width() / 2, vp->height() / 2));
}

void ZoomController::updateScrollRange()
{
    if (m_layout) m_layout->activate();
    // QScrollArea recomputes the canvas size and the scrollbar ranges when it
    // handles a layout request; the posted one only arrives after the current
    // event returns, which is too late for the scroll anchoring in applyZoom().
    QEvent layoutRequest(QEvent::LayoutRequest);
    QCoreApplication::sendEvent(m_area, &layoutRequest);
}

void ZoomController::applyZoom(int percent, const QPoint &viewportAnchor)
{
    if (m_zoom == percent || percent <= 0) return;
    const int   oldZoom  = m_zoom;
    const qreal oldScale = m_canvas->screenScale();

    // What sits under the anchor right now, in PDF points on a page — the one
    // thing that must not move. Canvas margins and page gaps do not scale with
    // the zoom, so scaling the scroll position alone drifts.
    const QPoint canvasAnchor = -m_canvas->canvasWidget()->pos() + viewportAnchor;
    auto [anchorPage, anchorLbl] = m_canvas->pageAtCanvasPos(canvasAnchor);
    QPointF anchorPt;
    if (anchorPage >= 0 && anchorLbl && oldScale > 0)
        anchorPt = (QPointF(canvasAnchor) - QPointF(anchorLbl->pos())) / oldScale;

    m_zoom = percent;
    m_engine->setZoom(percent);   // resizes the pages, renders them shortly after
    Q_EMIT zoomChanged(percent);
    updateScrollRange();

    const QLabel *anchorNow = anchorPage >= 0 ? m_canvas->pageLabel(anchorPage)
                                              : nullptr;
    int tx, ty;
    if (anchorNow) {
        const QPointF target =
            QPointF(anchorNow->pos()) + anchorPt * m_canvas->screenScale();
        tx = qRound(target.x()) - viewportAnchor.x();
        ty = qRound(target.y()) - viewportAnchor.y();
    } else {
        // The anchor sat in a margin or between two pages: no page point to
        // hold on to, so scale the scroll position instead.
        const qreal ratio = qreal(percent) / oldZoom;
        tx = qRound(ratio * (m_area->horizontalScrollBar()->value() + viewportAnchor.x()))
             - viewportAnchor.x();
        ty = qRound(ratio * (m_area->verticalScrollBar()->value() + viewportAnchor.y()))
             - viewportAnchor.y();
    }
    m_area->horizontalScrollBar()->setValue(tx);
    m_area->verticalScrollBar()->setValue(ty);
    // The scroll area can still be a layout pass behind, in which case the
    // values above were clamped to the old range. Retry once it has caught
    // up — the target is absolute, so this is idempotent.
    if (m_area->horizontalScrollBar()->value() != tx
        || m_area->verticalScrollBar()->value() != ty) {
        QTimer::singleShot(0, this, [this, tx, ty]() {
            m_area->horizontalScrollBar()->setValue(tx);
            m_area->verticalScrollBar()->setValue(ty);
            Q_EMIT viewportChanged();
        });
    }
    Q_EMIT viewportChanged();

    // Page geometry is final as far as this controller is concerned. Overlays
    // that need to settle further defer that themselves — the layout can still
    // run another pass, and only the owner of an overlay knows whether that
    // matters to it.
    Q_EMIT zoomApplied(percent);
}

void ZoomController::setSettings(int step, bool ctrlWheel, bool toPointer,
                                 const QString &wheelAction)
{
    m_zoomStep         = step;
    m_ctrlWheelEnabled = ctrlWheel;
    m_zoomToPointer    = toPointer;
    m_wheelAction      = wheelAction;
}

bool ZoomController::handleWheel(QWheelEvent *e)
{
    const bool hasCtrl = e->modifiers() & Qt::ControlModifier;
    const bool shouldZoom = (hasCtrl && m_ctrlWheelEnabled)
                         || (!hasCtrl && m_wheelAction == QLatin1String("zoom"));
    if (!shouldZoom) return false;

    const int delta = e->angleDelta().y();
    if (delta == 0) return false;

    const int step = qMax(1, m_zoomStep);
    const int next = delta > 0 ? qMin(m_zoom + step, 300)
                               : qMax(m_zoom - step, 25);

    // Anchor on the pointer position CARRIED BY THE EVENT. QCursor::pos()
    // has no meaning on Wayland (a client cannot query the pointer), so it
    // used to anchor the zoom at a stale point and threw the page around.
    QWidget *vp = m_area->viewport();
    const QPoint anchor = m_zoomToPointer
                              ? vp->mapFromGlobal(e->globalPosition().toPoint())
                              : QPoint(vp->width() / 2, vp->height() / 2);
    applyZoom(next, anchor);

    e->accept();
    return true;
}
