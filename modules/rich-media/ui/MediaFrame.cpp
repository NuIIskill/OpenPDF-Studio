// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/ui/MediaFrame.hpp"

#include "ui/theme/Theme.hpp"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

const QColor kAccent      { 0x3B, 0x82, 0xF6 };
const QColor kHandleFill  { 0xFF, 0xFF, 0xFF };

} // namespace

MediaFrame::MediaFrame(Mode mode, QWidget *parent)
    : QWidget(parent)
    , m_mode(mode)
{
    setMouseTracking(true);
    setFocusPolicy(mode == Mode::Placement ? Qt::StrongFocus : Qt::NoFocus);
    setCursor(mode == Mode::Placement ? Qt::SizeAllCursor : Qt::PointingHandCursor);
    setAttribute(Qt::WA_NoMousePropagation, true);
}

void MediaFrame::setPoster(const QImage &poster)
{
    m_poster = poster;
    update();
}

void MediaFrame::setCaption(const QString &caption)
{
    m_caption = caption;
    setToolTip(caption);
    update();
}

void MediaFrame::setInteractive(bool interactive)
{
    if (m_interactive == interactive) return;
    m_interactive = interactive;
    if (!interactive) setSelected(false);
    setFocusPolicy(interactive || m_mode == Mode::Placement ? Qt::StrongFocus
                                                           : Qt::NoFocus);
    setCursor(m_mode == Mode::Existing && !interactive ? Qt::PointingHandCursor
                                                       : Qt::ArrowCursor);
    update();
}

void MediaFrame::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    if (selected) setFocus(Qt::OtherFocusReason);
    update();
}

void MediaFrame::setCoverColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_coverColor = color;
    update();
}

void MediaFrame::setMode(Mode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    m_selected = false;
    setCursor(mode == Mode::Removed ? Qt::ArrowCursor : Qt::PointingHandCursor);
    update();
}

// ── Painting ─────────────────────────────────────────────────────────────────

QRect MediaFrame::playButtonRect() const
{
    const int size = qBound(28, qMin(width(), height()) / 4, 72);
    return QRect((width() - size) / 2, (height() - size) / 2, size, size);
}

void MediaFrame::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Marked for removal: the spot is covered because the poster only leaves
    // the document on save. Same approach the Core takes for deleted text.
    if (m_mode == Mode::Removed) {
        painter.fillRect(rect(), m_coverColor);
        QPen removedPen(QColor(0x94, 0xA3, 0xB8), 1, Qt::DashLine);
        painter.setPen(removedPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
        return;
    }

    const QRect inner = rect().adjusted(kHandle / 2, kHandle / 2,
                                        -kHandle / 2, -kHandle / 2);
    const QRect body  = m_mode == Mode::Placement ? inner : rect();

    // The frame draws the poster only while it is not in the document yet.
    // For a medium already in the file the page draws its appearance stream,
    // and a second image on top of that only flickers.
    if (!m_poster.isNull())
        painter.drawImage(body, m_poster, m_poster.rect());
    else if (m_mode == Mode::Placement)
        painter.fillRect(body, QColor(0x0F, 0x17, 0x2A, 30));

    if (m_mode == Mode::Existing && m_hovered)
        painter.fillRect(body, QColor(0, 0, 0, 46));

    // Rahmen
    QPen pen(kAccent, m_mode == Mode::Placement || m_selected ? 1.0 : 2.0);
    if (m_mode == Mode::Placement || m_selected) pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(body.adjusted(0, 0, -1, -1));

    // Play mark: always for an existing medium, while placing only when
    // there is no poster, or it would sit on top of itself.
    const bool drawPlay = m_mode == Mode::Existing || m_poster.isNull();
    if (drawPlay) {
        const QRect button = playButtonRect();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, m_hovered ? 250 : 225));
        painter.drawEllipse(button);

        const QPointF centre = QRectF(button).center();
        const qreal   side   = button.width() * 0.30;
        QPainterPath triangle;
        triangle.moveTo(centre.x() - side * 0.45, centre.y() - side * 0.72);
        triangle.lineTo(centre.x() - side * 0.45, centre.y() + side * 0.72);
        triangle.lineTo(centre.x() + side * 0.75, centre.y());
        triangle.closeSubpath();
        painter.setBrush(QColor(0x1E, 0x22, 0x2B));
        painter.drawPath(triangle);
    }

    // Handles: always while placing, for an existing medium only once it is
    // selected.
    if (m_mode == Mode::Placement || m_selected) {
        painter.setPen(QPen(kAccent, 1));
        painter.setBrush(kHandleFill);
        for (int h = static_cast<int>(Handle::TopLeft);
             h <= static_cast<int>(Handle::Left); ++h)
            painter.drawRect(handleRect(static_cast<Handle>(h)));
    }
}

// ── Handles ──────────────────────────────────────────────────────────────────

QRect MediaFrame::handleRect(Handle handle) const
{
    const int w = width(), h = height(), s = kHandle;
    const int cx = (w - s) / 2, cy = (h - s) / 2, right = w - s, bottom = h - s;
    switch (handle) {
    case Handle::TopLeft:     return { 0,     0,      s, s };
    case Handle::Top:         return { cx,    0,      s, s };
    case Handle::TopRight:    return { right, 0,      s, s };
    case Handle::Right:       return { right, cy,     s, s };
    case Handle::BottomRight: return { right, bottom, s, s };
    case Handle::Bottom:      return { cx,    bottom, s, s };
    case Handle::BottomLeft:  return { 0,     bottom, s, s };
    case Handle::Left:        return { 0,     cy,     s, s };
    default:                  return {};
    }
}

MediaFrame::Handle MediaFrame::handleAt(const QPoint &pos) const
{
    if (m_mode != Mode::Placement) return rect().contains(pos) ? Handle::Body : Handle::None;
    for (int h = static_cast<int>(Handle::TopLeft);
         h <= static_cast<int>(Handle::Left); ++h) {
        const auto handle = static_cast<Handle>(h);
        if (handleRect(handle).adjusted(-2, -2, 2, 2).contains(pos)) return handle;
    }
    return rect().contains(pos) ? Handle::Body : Handle::None;
}

void MediaFrame::applyCursor(Handle handle)
{
    switch (handle) {
    case Handle::TopLeft:
    case Handle::BottomRight: setCursor(Qt::SizeFDiagCursor); break;
    case Handle::TopRight:
    case Handle::BottomLeft:  setCursor(Qt::SizeBDiagCursor); break;
    case Handle::Top:
    case Handle::Bottom:      setCursor(Qt::SizeVerCursor);   break;
    case Handle::Left:
    case Handle::Right:       setCursor(Qt::SizeHorCursor);   break;
    case Handle::Body:        setCursor(Qt::SizeAllCursor);   break;
    default:                  setCursor(Qt::ArrowCursor);     break;
    }
}

// ── Mouse ────────────────────────────────────────────────────────────────────

void MediaFrame::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) { event->ignore(); return; }

    if (m_mode == Mode::Removed) { event->accept(); return; }

    if (m_mode == Mode::Existing) {
        // The play badge always plays, even with the tool active. Anywhere
        // else selects while the tool is active, so a medium can be removed
        // without starting it first.
        if (m_interactive && !playButtonRect().contains(event->pos()))
            setSelected(true);
        else
            Q_EMIT activated();
        event->accept();
        return;
    }

    m_active          = handleAt(event->pos());
    m_pressPos        = event->globalPosition().toPoint();
    m_geometryAtPress = geometry();
    setFocus(Qt::MouseFocusReason);
    event->accept();
}

void MediaFrame::mouseMoveEvent(QMouseEvent *event)
{
    if (m_mode != Mode::Placement) return;

    if (m_active == Handle::None) {
        applyCursor(handleAt(event->pos()));
        return;
    }

    const QPoint delta = event->globalPosition().toPoint() - m_pressPos;
    QRect target = m_geometryAtPress;

    switch (m_active) {
    case Handle::Body:        target.translate(delta);                        break;
    case Handle::TopLeft:     target.setTopLeft(target.topLeft() + delta);    break;
    case Handle::Top:         target.setTop(target.top() + delta.y());        break;
    case Handle::TopRight:    target.setTopRight(target.topRight() + delta);  break;
    case Handle::Right:       target.setRight(target.right() + delta.x());    break;
    case Handle::BottomRight: target.setBottomRight(target.bottomRight() + delta); break;
    case Handle::Bottom:      target.setBottom(target.bottom() + delta.y());  break;
    case Handle::BottomLeft:  target.setBottomLeft(target.bottomLeft() + delta); break;
    case Handle::Left:        target.setLeft(target.left() + delta.x());      break;
    case Handle::None:        return;
    }

    target = target.normalized();
    if (target.width()  < kMinSize) target.setWidth(kMinSize);
    if (target.height() < kMinSize) target.setHeight(kMinSize);

    // Stay on the page: a medium half beside the paper is an annotation no
    // viewer shows sensibly.
    if (!m_pageRect.isNull()) {
        if (target.width()  > m_pageRect.width())  target.setWidth(m_pageRect.width());
        if (target.height() > m_pageRect.height()) target.setHeight(m_pageRect.height());
        if (target.left()   < m_pageRect.left())   target.moveLeft(m_pageRect.left());
        if (target.top()    < m_pageRect.top())    target.moveTop(m_pageRect.top());
        if (target.right()  > m_pageRect.right())  target.moveRight(m_pageRect.right());
        if (target.bottom() > m_pageRect.bottom()) target.moveBottom(m_pageRect.bottom());
    }

    setGeometry(target);
    Q_EMIT geometryEdited(target);
}

void MediaFrame::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_mode == Mode::Existing) Q_EMIT activated();
    event->accept();
}

void MediaFrame::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_mode == Mode::Removed) { event->ignore(); return; }
    if (m_mode == Mode::Existing) setSelected(true);
    Q_EMIT contextMenuRequested(event->globalPos());
    event->accept();
}

void MediaFrame::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    m_active = Handle::None;
}

void MediaFrame::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event)
    m_hovered = true;
    update();
}

void MediaFrame::leaveEvent(QEvent *event)
{
    Q_UNUSED(event)
    m_hovered = false;
    update();
}

void MediaFrame::keyPressEvent(QKeyEvent *event)
{
    const bool deleteKey = event->key() == Qt::Key_Delete
                        || event->key() == Qt::Key_Backspace;
    if (deleteKey && (m_mode == Mode::Placement
                      || (m_mode == Mode::Existing && m_selected))) {
        Q_EMIT deleteRequested();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_selected) {
        setSelected(false);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
