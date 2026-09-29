#include "ui/view/SignaturePlacement.hpp"
#include "ui/view/PageCanvas.hpp"

#include <QAbstractScrollArea>
#include <QCursor>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <utility>

namespace {

constexpr qreal kDefaultWidthPt = 160.0;
constexpr qreal kMaxHeightPt    = 80.0;
constexpr qreal kGhostOpacity   = 0.65;

}

SignaturePlacement::SignaturePlacement(PageCanvas *canvas, QAbstractScrollArea *view,
                                       QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
    , m_view(view)
{
}

void SignaturePlacement::start(const QImage &image)
{
    if (image.isNull() || m_canvas->pageLabelCount() == 0) return;
    cancel();

    m_image = image;
    qreal w = kDefaultWidthPt;
    qreal h = w * image.height() / qMax(1, image.width());
    if (h > kMaxHeightPt) { w *= kMaxHeightPt / h; h = kMaxHeightPt; }
    m_sizePt = QSizeF(w, h);

    if (!m_ghost) {
        m_ghost = new QLabel(m_canvas->canvasWidget());
        m_ghost->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
    m_ghost->resize(0, 0);
    m_active = true;
    setListening(true);
    m_view->setFocus(Qt::OtherFocusReason);

    const QPoint vpPos = m_view->viewport()->mapFromGlobal(QCursor::pos());
    if (m_view->viewport()->rect().contains(vpPos))
        moveGhost(m_canvas->canvasWidget()->mapFromGlobal(QCursor::pos()));
}

void SignaturePlacement::startField(const QImage &preview, FieldPlaced onPlaced)
{
    start(preview);
    if (m_active) m_onField = std::move(onPlaced);
}

void SignaturePlacement::cancel()
{
    m_onField = {};
    m_active = false;
    m_swallowRelease = false;
    if (m_ghost) m_ghost->hide();
    setListening(false);
}

QRect SignaturePlacement::ghostRect(const QPoint &canvasPos) const
{
    const qreal scale = m_canvas->screenScale();
    QRect r(QPoint(), (m_sizePt * scale).toSize().expandedTo(QSize(1, 1)));
    r.moveCenter(canvasPos);

    const QLabel *page = m_canvas->pageAtCanvasPos(canvasPos).second;
    if (!page) return r;
    const QRect g = page->geometry();
    r.moveLeft(std::max(g.left(), std::min(r.left(), g.right() - r.width() + 1)));
    r.moveTop(std::max(g.top(), std::min(r.top(), g.bottom() - r.height() + 1)));
    return r;
}

void SignaturePlacement::moveGhost(const QPoint &canvasPos)
{
    const QRect r = ghostRect(canvasPos);
    if (m_ghost->size() != r.size()) {
        QPixmap px(r.size());
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setOpacity(kGhostOpacity);
        p.drawImage(px.rect(), m_image);
        p.end();
        m_ghost->setPixmap(px);
    }
    m_ghost->setGeometry(r);
    m_ghost->raise();
    m_ghost->show();
}

void SignaturePlacement::setListening(bool on)
{
    if (on == m_listening) return;
    m_listening = on;

    QWidget *canvas   = m_canvas->canvasWidget();
    QWidget *viewport = m_view->viewport();
    if (on) {
        m_canvasTracked   = canvas->hasMouseTracking();
        m_viewportTracked = viewport->hasMouseTracking();
        canvas->setMouseTracking(true);
        viewport->setMouseTracking(true);
        for (QObject *o : { static_cast<QObject *>(canvas), static_cast<QObject *>(viewport),
                            static_cast<QObject *>(m_view) })
            o->installEventFilter(this);
    } else {
        canvas->setMouseTracking(m_canvasTracked);
        viewport->setMouseTracking(m_viewportTracked);
        for (QObject *o : { static_cast<QObject *>(canvas), static_cast<QObject *>(viewport),
                            static_cast<QObject *>(m_view) })
            o->removeEventFilter(this);
    }
}

bool SignaturePlacement::eventFilter(QObject *obj, QEvent *e)
{
    QWidget *canvas = m_canvas->canvasWidget();
    const bool mouseSource = obj == canvas || obj == m_view->viewport();

    switch (e->type()) {
    case QEvent::MouseMove:
        if (!mouseSource || !m_active) break;
        moveGhost(canvas->mapFromGlobal(static_cast<QMouseEvent *>(e)->globalPosition().toPoint()));
        return true;

    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        if (!mouseSource || !m_active) break;
        auto *me = static_cast<QMouseEvent *>(e);
        const QPoint pos = canvas->mapFromGlobal(me->globalPosition().toPoint());
        if (me->button() == Qt::LeftButton && m_canvas->pageAtCanvasPos(pos).first < 0)
            return true;

        m_active = false;
        m_swallowRelease = true;
        m_ghost->hide();
        FieldPlaced onField = std::exchange(m_onField, {});
        if (me->button() != Qt::LeftButton) return true;

        const QRect r = ghostRect(pos);
        if (!onField) {
            Q_EMIT placed(m_image, r);
            return true;
        }
        const auto [page, label] = m_canvas->pageAtCanvasPos(r.topLeft());
        if (page < 0 || !label) return true;
        const qreal scale = m_canvas->screenScale();
        const QPoint local = r.topLeft() - label->pos();
        const QRectF bounds(local.x() / scale, local.y() / scale,
                            r.width() / scale, r.height() / scale);
        // Queued: the callback may open dialogs, which must not run inside
        // this mouse press.
        QMetaObject::invokeMethod(this, [onField, page, bounds]() { onField(page, bounds); },
                                  Qt::QueuedConnection);
        return true;
    }

    case QEvent::MouseButtonRelease:
        if (!mouseSource) break;
        if (m_swallowRelease) {
            m_swallowRelease = false;
            setListening(false);
            return true;
        }
        return m_active;

    case QEvent::KeyPress:
        if (m_active && static_cast<QKeyEvent *>(e)->key() == Qt::Key_Escape) {
            cancel();
            return true;
        }
        break;

    case QEvent::Leave:
        if (obj == m_view->viewport() && m_ghost) m_ghost->hide();
        break;

    default:
        break;
    }
    return false;
}
