#include "ui/forms/FormDesigner.hpp"

#include "ui/DocumentView.hpp"
#include "ui/forms/FormFieldPanel.hpp"
#include "ui/forms/FormLayer.hpp"
#include "ui/theme/Theme.hpp"

#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>

namespace {

constexpr int   kGrip     = 6;
constexpr qreal kSmallest = 6.0;

QPointer<FormDesigner> g_bound;

QSizeF defaultSize(FormSpec::Kind kind)
{
    switch (kind) {
    case FormSpec::Kind::CheckBox:
    case FormSpec::Kind::Radio:    return { 14, 14 };
    case FormSpec::Kind::ComboBox: return { 140, 22 };
    case FormSpec::Kind::ListBox:  return { 100, 60 };
    case FormSpec::Kind::Text:     break;
    }
    return { 160, 22 };
}

}

/// One field widget as the design tool shows it: an outline, and when picked a filled frame with corner marks.
class FormFrame : public QWidget
{
public:
    static constexpr int kMargin = 7;

    FormFrame(FormDesigner *designer, int index, const FormLayer::Place &place, QWidget *parent)
        : QWidget(parent), m_designer(designer), m_index(index), m_place(place)
    {
        setMouseTracking(true);
        setToolTip(place.field);
        hide();
    }

    const FormLayer::Place &place() const { return m_place; }
    bool selected() const { return m_selected; }

    void setSelected(bool selected)
    {
        if (m_selected == selected) return;
        m_selected = selected;
        update();
    }

    QRect field() const
    {
        const int m = m_selected ? kMargin : 0;
        return rect().adjusted(m, m, -m, -m);
    }

    int cornerAt(const QPoint &pos) const
    {
        if (!m_selected) return -1;
        const QRect f = field();
        const QPoint corners[] = { f.topLeft(), f.topRight(), f.bottomLeft(), f.bottomRight() };
        for (int i = 0; i < 4; ++i)
            if ((pos - corners[i]).manhattanLength() <= kGrip * 2) return i;
        return -1;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF f = QRectF(field()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool sketch = m_index < 0;
        QColor fill = Theme::Primary;
        QColor line = Theme::Primary;
        if (m_selected || sketch) {
            fill.setAlpha(Theme::DarkMode ? 56 : 30);
            painter.setPen(QPen(line, sketch ? 1.5 : 2.0, sketch ? Qt::DashLine : Qt::SolidLine));
            painter.setBrush(fill);
            painter.drawRoundedRect(f.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
        } else {
            fill.setAlpha(m_hover ? 22 : 0);
            line.setAlpha(m_hover ? 220 : 110);
            painter.setPen(QPen(line, 1.0));
            painter.setBrush(fill);
            painter.drawRect(f);
        }
        if (!m_selected) return;
        // Marks just outside the top left and bottom right corner, where the frame is resized.
        constexpr qreal gap = 4, arm = 9;
        painter.setPen(QPen(Theme::Primary, 2.0, Qt::SolidLine, Qt::SquareCap));
        painter.setBrush(Qt::NoBrush);
        const QPointF tl = f.topLeft() - QPointF(gap, gap), br = f.bottomRight() + QPointF(gap, gap);
        painter.drawPolyline(QPolygonF { tl + QPointF(0, arm), tl, tl + QPointF(arm, 0) });
        painter.drawPolyline(QPolygonF { br - QPointF(0, arm), br, br - QPointF(arm, 0) });
    }

    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) return QWidget::mousePressEvent(event);
        event->accept();
        m_designer->framePress(m_index, mapToParent(event->position().toPoint()));
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton) {
            m_designer->dragTo(mapToParent(event->position().toPoint()));
            return;
        }
        const int corner = cornerAt(event->position().toPoint());
        setCursor(corner < 0 ? Qt::SizeAllCursor : corner == 0 || corner == 3 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) m_designer->release();
    }

private:
    FormDesigner     *m_designer;
    int               m_index;
    FormLayer::Place  m_place;
    bool              m_selected { false };
    bool              m_hover { false };
};

FormDesigner::FormDesigner(FormLayer *layer, DocumentView *view, QObject *parent)
    : QObject(parent)
    , m_layer(layer)
    , m_view(view)
{}

FormDesigner::~FormDesigner()
{
    for (const QPointer<FormFrame> &frame : std::as_const(m_frames)) delete frame.data();
    delete m_sketch.data();
}

void FormDesigner::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    QWidget *canvas = m_view ? m_view->canvasWidget() : nullptr;
    if (active) {
        if (canvas) canvas->installEventFilter(this);
        if (m_view) m_view->installEventFilter(this);
        bindPanel();
    } else {
        setPlacing(std::nullopt);
        if (canvas) {
            canvas->removeEventFilter(this);
            canvas->unsetCursor();
        }
        if (m_view) m_view->removeEventFilter(this);
        m_drag = Drag::None;
    }
    relayout();
}

void FormDesigner::rebuild()
{
    // A frame may be rebuilt from inside its own mouse event, so it goes once that event is done.
    for (const QPointer<FormFrame> &frame : std::as_const(m_frames))
        if (frame) {
            frame->hide();
            frame->deleteLater();
        }
    m_frames.clear();
    m_drag = Drag::None;
    QWidget *canvas = m_view ? m_view->canvasWidget() : nullptr;
    if (!canvas) return;
    const QList<FormLayer::Place> places = m_layer->places();
    for (int i = 0; i < places.size(); ++i) m_frames.append(new FormFrame(this, i, places.at(i), canvas));
    if (!m_layer->hasField(m_field)) m_field.clear();
    relayout();
    refreshPanel();
}

void FormDesigner::relayout()
{
    const bool show = shown();
    for (int i = 0; i < m_frames.size(); ++i) {
        FormFrame *frame = m_frames.at(i);
        if (!frame) continue;
        const FormLayer::Place &p = frame->place();
        const bool dragged = i == m_frame && (m_drag == Drag::Move || m_drag == Drag::Resize);
        const QRect area = show ? toCanvas(p.page, dragged ? m_dragBounds : p.bounds) : QRect();
        frame->setVisible(!area.isNull());
        if (area.isNull()) continue;
        frame->setSelected(p.field == m_field && p.index == m_index);
        const int m = frame->selected() ? FormFrame::kMargin : 0;
        frame->setGeometry(area.adjusted(-m, -m, m, m));
        frame->raise();
    }
    if (m_sketch) {
        const QRect area = show && (m_drag == Drag::Create || m_drag == Drag::Drop) ? toCanvas(m_page, m_dragBounds) : QRect();
        m_sketch->setVisible(!area.isNull());
        if (!area.isNull()) {
            m_sketch->setGeometry(area);
            m_sketch->raise();
        }
    }
}

void FormDesigner::select(const QString &field, int index)
{
    m_field = field;
    m_index = index;
    relayout();
    refreshPanel();
}

bool FormDesigner::shown() const
{
    return m_active && m_view && m_view->pageCount() > 0
        && m_view->viewMode() == DocumentView::ViewMode::Single;
}

bool FormDesigner::eventFilter(QObject *watched, QEvent *event)
{
    if (!shown()) return false;
    if (watched == m_view && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if ((key == Qt::Key_Delete || key == Qt::Key_Backspace) && !m_field.isEmpty()) {
            m_layer->removeField(m_field);
            return true;
        }
        if (key == Qt::Key_Escape && m_placing) {
            setPlacing(std::nullopt);
            return true;
        }
        if (key == Qt::Key_Escape && !m_field.isEmpty()) {
            select(QString());
            return true;
        }
        return false;
    }
    if (m_view && watched == m_view->canvasWidget()) {
        if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseMove
                && event->type() != QEvent::MouseButtonRelease)
            return false;
        return canvasMouse(event, static_cast<QMouseEvent *>(event)->position().toPoint());
    }
    return false;
}

bool FormDesigner::canvasMouse(QEvent *event, const QPoint &pos)
{
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (event->type() == QEvent::MouseButtonPress) {
        if (mouse->button() != Qt::LeftButton) return false;
        const auto [page, label] = m_view->pageAtCanvasPos(pos);
        if (page < 0 || !label) {
            select(QString());
            m_drag = Drag::Swallow;
            return true;
        }
        // A drag on the page draws a field; a plain click places a chosen kind or clears the pick.
        m_drag     = Drag::Create;
        m_page     = page;
        m_pressPos = pos;
        const qreal scale = m_view->screenScale();
        m_dragBounds = QRectF((QPointF(pos) - QPointF(label->pos())) / scale, QSizeF());
        m_pressBounds = m_dragBounds;
        if (!m_sketch) m_sketch = new FormFrame(this, -1, { tr("New field"), 0, -1, {}, m_lastKind },
                                                m_view->canvasWidget());
        relayout();
        return true;
    }
    if (m_drag == Drag::None) return false;
    if (m_drag == Drag::Swallow) {
        if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton) m_drag = Drag::None;
        return true;
    }
    if (event->type() == QEvent::MouseMove) dragTo(pos);
    else if (mouse->button() == Qt::LeftButton) release();
    return true;
}

void FormDesigner::framePress(int frame, const QPoint &canvasPos)
{
    FormFrame *f = m_frames.value(frame);
    if (!f) return;
    const FormLayer::Place &p = f->place();
    const bool wasSelected = p.field == m_field && p.index == m_index;
    m_corner = wasSelected ? f->cornerAt(f->mapFromParent(canvasPos)) : -1;
    if (!wasSelected) select(p.field, p.index);
    m_drag        = m_corner >= 0 ? Drag::Resize : Drag::Move;
    m_frame       = frame;
    m_page        = p.page;
    m_pressPos    = canvasPos;
    m_pressBounds = p.bounds;
    m_dragBounds  = p.bounds;
}

void FormDesigner::dragTo(const QPoint &canvasPos)
{
    if (m_drag == Drag::None) return;
    const qreal scale = m_view->screenScale();
    const QPointF delta = QPointF(canvasPos - m_pressPos) / scale;
    const QRectF page = pageRect(m_page);
    QRectF r = m_pressBounds;
    switch (m_drag) {
    case Drag::Create:
        r = QRectF(m_pressBounds.topLeft(), m_pressBounds.topLeft() + delta).normalized();
        break;
    case Drag::Move:
        r.translate(delta);
        r.moveLeft(qBound(page.left(), r.left(), page.right() - r.width()));
        r.moveTop(qBound(page.top(), r.top(), page.bottom() - r.height()));
        break;
    case Drag::Resize:
        if (m_corner == 0 || m_corner == 2) r.setLeft(qMin(r.left() + delta.x(), r.right() - kSmallest));
        else                                r.setRight(qMax(r.right() + delta.x(), r.left() + kSmallest));
        if (m_corner == 0 || m_corner == 1) r.setTop(qMin(r.top() + delta.y(), r.bottom() - kSmallest));
        else                                r.setBottom(qMax(r.bottom() + delta.y(), r.top() + kSmallest));
        break;
    case Drag::None:
        break;
    }
    m_dragBounds = r.intersected(page);
    relayout();
}

void FormDesigner::release()
{
    const Drag drag = m_drag;
    m_drag = Drag::None;
    QRectF bounds = m_dragBounds;
    if (drag == Drag::Create) {
        const bool clicked = bounds.width() < 4 && bounds.height() < 4;
        if (clicked && !m_placing) {
            select(QString());
        } else {
            const FormSpec::Kind kind = m_placing.value_or(m_lastKind);
            setPlacing(std::nullopt);
            if (clicked) bounds = QRectF(m_pressBounds.topLeft(), defaultSize(kind));
            add(kind, bounds);
        }
    } else if ((drag == Drag::Move || drag == Drag::Resize) && bounds != m_pressBounds) {
        FormSpec spec = m_layer->specFor(m_field);
        if (m_index >= 0 && m_index < spec.places.size()) {
            spec.places[m_index].bounds = bounds;
            m_layer->commitSpec(m_field, spec, drag == Drag::Move ? tr("Form field moved")
                                                                  : tr("Form field resized"));
        }
    }
    m_frame = -1;
    relayout();
}

void FormDesigner::dragKind(FormSpec::Kind kind, const QPoint &global)
{
    if (!shown()) return;
    QWidget *canvas = m_view->canvasWidget();
    const QPoint pos = canvas->mapFromGlobal(global);
    // The canvas reaches past the visible part, so a point outside the viewport is not on a page.
    QWidget *viewport = canvas->parentWidget();
    const bool visible = !viewport || viewport->rect().contains(viewport->mapFromGlobal(global));
    const auto [page, label] = visible ? m_view->pageAtCanvasPos(pos) : std::pair<int, QLabel *>(-1, nullptr);
    FormFieldPanel *panel = FormFieldPanel::instance();
    if (page < 0 || !label) {
        m_drag = Drag::None;
        if (panel) panel->setGhostShown(true);
        relayout();
        return;
    }
    // Over a page the field itself shows where it lands, centred on the pointer.
    const QSizeF size = defaultSize(kind);
    const QRectF pageBounds = pageRect(page);
    QRectF r(QPointF(pos - label->pos()) / m_view->screenScale() - QPointF(size.width(), size.height()) / 2, size);
    r.moveLeft(qBound(pageBounds.left(), r.left(), qMax(pageBounds.left(), pageBounds.right() - r.width())));
    r.moveTop(qBound(pageBounds.top(), r.top(), qMax(pageBounds.top(), pageBounds.bottom() - r.height())));
    m_drag       = Drag::Drop;
    m_page       = page;
    m_dragBounds = r.intersected(pageBounds);
    if (!m_sketch) m_sketch = new FormFrame(this, -1, { tr("New field"), 0, -1, {}, kind }, m_view->canvasWidget());
    if (panel) panel->setGhostShown(false);
    relayout();
}

void FormDesigner::dropKind(FormSpec::Kind kind, const QPoint &global)
{
    dragKind(kind, global);
    const bool onPage = m_drag == Drag::Drop;
    m_drag = Drag::None;
    if (onPage) {
        setPlacing(std::nullopt);
        add(kind, m_dragBounds);
    }
    relayout();
}

void FormDesigner::add(FormSpec::Kind kind, QRectF bounds)
{
    const QRectF page = pageRect(m_page);
    bounds.moveLeft(qBound(page.left(), bounds.left(), qMax(page.left(), page.right() - bounds.width())));
    bounds.moveTop(qBound(page.top(), bounds.top(), qMax(page.top(), page.bottom() - bounds.height())));
    bounds = bounds.intersected(page);
    const QString group = kind == FormSpec::Kind::Radio
                              && m_layer->specFor(m_field).kind == FormSpec::Kind::Radio
                          ? m_field : QString();
    m_lastKind = kind;
    const QString name = m_layer->addField(kind, m_page, bounds, group);
    if (!name.isEmpty()) select(name, int(m_layer->specFor(name).places.size()) - 1);
}

QRectF FormDesigner::pageRect(int page) const
{
    const QLabel *label = m_view ? m_view->pageLabel(page) : nullptr;
    if (!label) return {};
    return QRectF(QPointF(), QSizeF(label->size()) / m_view->screenScale());
}

QRect FormDesigner::toCanvas(int page, const QRectF &bounds) const
{
    const QLabel *label = m_view ? m_view->pageLabel(page) : nullptr;
    if (!label || bounds.isNull()) return {};
    const qreal scale = m_view->screenScale();
    return QRectF(bounds.topLeft() * scale + QPointF(label->pos()), bounds.size() * scale).toAlignedRect();
}

void FormDesigner::bindPanel()
{
    FormFieldPanel *panel = FormFieldPanel::instance();
    if (!panel) return;
    if (g_bound != this) {
        if (g_bound) QObject::disconnect(panel, nullptr, g_bound.data(), nullptr);
        connect(panel, &FormFieldPanel::kindChosen, this, [this](FormSpec::Kind kind) {
            setPlacing(m_placing == kind ? std::nullopt : std::optional(kind));
        });
        connect(panel, &FormFieldPanel::kindDragged, this, &FormDesigner::dragKind);
        connect(panel, &FormFieldPanel::kindDropped, this, &FormDesigner::dropKind);
        connect(panel, &FormFieldPanel::specEdited, this, [this](const FormSpec &spec) {
            if (m_field.isEmpty()) return;
            if (m_layer->commitSpec(m_field, spec, tr("Form field changed"))) select(spec.name, m_index);
            else refreshPanel();
        });
        connect(panel, &FormFieldPanel::removeRequested, this, [this] {
            if (!m_field.isEmpty()) m_layer->removeField(m_field);
        });
        g_bound = this;
    }
    panel->setPlacement(m_placing);
    refreshPanel();
}

void FormDesigner::setPlacing(std::optional<FormSpec::Kind> kind)
{
    m_placing = kind;
    if (QWidget *canvas = m_view ? m_view->canvasWidget() : nullptr) {
        if (kind) canvas->setCursor(Qt::CrossCursor);
        else      canvas->unsetCursor();
    }
    if (FormFieldPanel *panel = FormFieldPanel::instance(); panel && g_bound == this) panel->setPlacement(kind);
}

void FormDesigner::refreshPanel()
{
    FormFieldPanel *panel = FormFieldPanel::instance();
    if (!panel || g_bound != this) return;
    if (m_field.isEmpty()) panel->showField(std::nullopt, 0);
    else                   panel->showField(m_layer->specFor(m_field), m_index);
}
