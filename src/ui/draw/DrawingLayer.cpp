#include "ui/draw/DrawingLayer.hpp"

#include <QLabel>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QUndoCommand>
#include <QUndoStack>
#include <QWidget>

namespace {

class StrokePreview final : public QWidget
{
public:
    explicit StrokePreview(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
    }

    void setStroke(const EditSession::DrawStroke &stroke, qreal scale)
    {
        m_stroke = stroke;
        m_scale = scale;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_stroke.points.isEmpty()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(m_stroke.color,
                            qMax<qreal>(0.5, m_stroke.widthPt * m_scale),
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (m_stroke.points.size() == 1) {
            painter.drawPoint(m_stroke.points.first() * m_scale);
            return;
        }
        QPainterPath path(m_stroke.points.first() * m_scale);
        for (int i = 1; i < m_stroke.points.size(); ++i)
            path.lineTo(m_stroke.points.at(i) * m_scale);
        painter.drawPath(path);
    }

private:
    EditSession::DrawStroke m_stroke;
    qreal m_scale { 1.0 };
};

qreal distanceToSegment(const QPointF &point, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal lengthSquared = QPointF::dotProduct(ab, ab);
    if (lengthSquared <= 0.0001) return QLineF(point, a).length();
    const qreal t = qBound<qreal>(0.0, QPointF::dotProduct(point - a, ab)
                                      / lengthSquared, 1.0);
    return QLineF(point, a + ab * t).length();
}

bool strokeHit(const EditSession::DrawStroke &stroke, const QPointF &point,
               qreal eraserRadius)
{
    if (stroke.points.isEmpty()) return false;
    const qreal radius = eraserRadius + stroke.widthPt / 2.0;
    if (stroke.points.size() == 1)
        return QLineF(point, stroke.points.first()).length() <= radius;
    for (int i = 1; i < stroke.points.size(); ++i)
        if (distanceToSegment(point, stroke.points.at(i - 1),
                              stroke.points.at(i)) <= radius)
            return true;
    return false;
}

}

class DrawingUndoCommand : public QUndoCommand
{
public:
    DrawingUndoCommand(DrawingLayer *layer,
                       QList<EditSession::DrawStroke> before,
                       QList<EditSession::DrawStroke> after,
                       const QString &text)
        : QUndoCommand(text)
        , m_layer(layer)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {}

    void undo() override { m_layer->restoreState(m_before); }
    void redo() override
    {
        if (m_firstRedo) { m_firstRedo = false; return; }
        m_layer->restoreState(m_after);
    }

private:
    DrawingLayer *m_layer { nullptr };
    QList<EditSession::DrawStroke> m_before;
    QList<EditSession::DrawStroke> m_after;
    bool m_firstRedo { true };
};

DrawingLayer::DrawingLayer(PageCanvas *canvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
    , m_preview(new StrokePreview(canvas->canvasWidget()))
{}

void DrawingLayer::setSource(EditSession *session, QUndoStack *undo)
{
    m_session = session;
    m_undo = undo;
}

void DrawingLayer::setActive(bool active)
{
    if (m_active == active) return;
    if (!active && m_drawing) finishGesture();
    m_active = active;
}

void DrawingLayer::setTool(DrawTool tool)
{
    if (m_tool == tool) return;
    if (m_drawing) finishGesture();
    m_tool = tool;
}

void DrawingLayer::setColor(const QColor &color)
{
    if (color.isValid()) m_color = color;
}

void DrawingLayer::setWidth(qreal widthPt)
{
    m_widthPt = qBound<qreal>(0.5, widthPt, 24.0);
}

bool DrawingLayer::handlePress(const QPoint &canvasPos)
{
    if (!m_active || !m_session) return false;
    const auto [page, label] = m_canvas->pageAtCanvasPos(canvasPos);
    if (page < 0 || !label) return false;

    m_beforeGesture = m_session->drawStrokes();
    m_activePage = page;
    m_drawing = true;
    m_changed = false;

    if (m_tool == DrawTool::Eraser) {
        eraseAt(canvasPos);
        return true;
    }

    m_activeStroke = {};
    m_activeStroke.page = page;
    m_activeStroke.color = m_color;
    m_activeStroke.widthPt = m_widthPt;
    if (m_tool == DrawTool::Highlighter) {
        m_activeStroke.color.setAlpha(92);
        m_activeStroke.widthPt = qMax<qreal>(6.0, m_widthPt * 3.2);
    } else {
        m_activeStroke.color.setAlpha(255);
    }
    appendPoint(canvasPos);
    updatePreview();
    return true;
}

bool DrawingLayer::handleMove(const QPoint &canvasPos)
{
    if (!m_drawing) return false;
    if (m_tool == DrawTool::Eraser) eraseAt(canvasPos);
    else if (appendPoint(canvasPos)) updatePreview();
    return true;
}

bool DrawingLayer::handleRelease()
{
    if (!m_drawing) return false;
    finishGesture();
    return true;
}

bool DrawingLayer::appendPoint(const QPoint &canvasPos)
{
    const QLabel *label = m_canvas->pageLabel(m_activePage);
    if (!label) return false;
    const qreal scale = m_canvas->screenScale();
    if (scale <= 0.0) return false;

    QPoint local = canvasPos - label->pos();
    local.setX(qBound(0, local.x(), label->width()));
    local.setY(qBound(0, local.y(), label->height()));
    const QPointF point = QPointF(local) / scale;
    if (!m_activeStroke.points.isEmpty()
            && QLineF(m_activeStroke.points.last(), point).length() < 0.35)
        return false;
    m_activeStroke.points.append(point);
    return true;
}

bool DrawingLayer::eraseAt(const QPoint &canvasPos)
{
    const auto [page, label] = m_canvas->pageAtCanvasPos(canvasPos);
    if (page != m_activePage || !label) return false;
    const qreal scale = m_canvas->screenScale();
    if (scale <= 0.0) return false;
    const QPointF point = QPointF(canvasPos - label->pos()) / scale;

    QList<EditSession::DrawStroke> strokes = m_session->drawStrokes();
    const qsizetype before = strokes.size();
    const qreal radius = qMax<qreal>(5.0, m_widthPt * 1.8);
    strokes.removeIf([&](const EditSession::DrawStroke &stroke) {
        return stroke.page == page && strokeHit(stroke, point, radius);
    });
    if (strokes.size() == before) return false;
    m_session->replaceDrawStrokes(std::move(strokes));
    m_changed = true;
    Q_EMIT pageNeedsRerender(page);
    return true;
}

void DrawingLayer::finishGesture()
{
    if (!m_drawing || !m_session) return;
    const int page = m_activePage;
    if (m_tool != DrawTool::Eraser && !m_activeStroke.points.isEmpty()) {
        QList<EditSession::DrawStroke> strokes = m_session->drawStrokes();
        strokes.append(m_activeStroke);
        m_session->replaceDrawStrokes(std::move(strokes));
        m_changed = true;
        Q_EMIT pageNeedsRerender(page);
    }

    static_cast<StrokePreview *>(m_preview)->hide();
    const QList<EditSession::DrawStroke> after = m_session->drawStrokes();
    if (m_changed) {
        pushUndo(m_beforeGesture, after,
                 m_tool == DrawTool::Eraser ? tr("Erase drawing")
                                            : tr("Draw stroke"));
        if (m_tool == DrawTool::Eraser) Q_EMIT strokesRemoved(page);
        else Q_EMIT strokeAdded(page);
    }
    m_activeStroke = {};
    m_beforeGesture.clear();
    m_activePage = -1;
    m_drawing = false;
    m_changed = false;
}

void DrawingLayer::clear()
{
    m_activeStroke = {};
    m_beforeGesture.clear();
    m_activePage = -1;
    m_drawing = false;
    m_changed = false;
    m_preview->hide();
}

void DrawingLayer::relayout()
{
    if (!m_drawing || m_activePage < 0) return;
    updatePreview();
}

void DrawingLayer::restoreState(const QList<EditSession::DrawStroke> &strokes)
{
    if (!m_session) return;
    QList<int> pages;
    for (const EditSession::DrawStroke &stroke : m_session->drawStrokes())
        if (!pages.contains(stroke.page)) pages.append(stroke.page);
    for (const EditSession::DrawStroke &stroke : strokes)
        if (!pages.contains(stroke.page)) pages.append(stroke.page);
    m_session->replaceDrawStrokes(strokes);
    for (int page : pages) Q_EMIT pageNeedsRerender(page);
}

void DrawingLayer::pushUndo(const QList<EditSession::DrawStroke> &before,
                            const QList<EditSession::DrawStroke> &after,
                            const QString &text)
{
    if (!m_undo || before == after) return;
    m_undo->push(new DrawingUndoCommand(this, before, after, text));
}

void DrawingLayer::updatePreview()
{
    if (m_activePage < 0 || m_tool == DrawTool::Eraser) return;
    const QLabel *label = m_canvas->pageLabel(m_activePage);
    if (!label) return;
    auto *preview = static_cast<StrokePreview *>(m_preview);
    preview->setGeometry(label->geometry());
    preview->setStroke(m_activeStroke, m_canvas->screenScale());
    preview->show();
    preview->raise();
}
