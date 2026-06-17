#include "TextBoxFrame.hpp"
#include "InlineEditor.hpp"

#include <QPainter>
#include <QPen>
#include <QMouseEvent>
#include <QTimer>
#include <QDebug>
#include <QFontMetrics>

// ── Construction ──────────────────────────────────────────────────────────────

TextBoxFrame::TextBoxFrame(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);   // never steal focus from InlineEditor
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setMouseTracking(true);
    hide();

    m_editor = new InlineEditor(this);
    connect(m_editor, &InlineEditor::committed, this, &TextBoxFrame::committed);
    connect(m_editor, &InlineEditor::cancelled,  this, &TextBoxFrame::cancelled);
}

QRect TextBoxFrame::innerRect() const
{
    return m_decorations ? rect().adjusted(kPad, kPad, -kPad, -kPad) : rect();
}

void TextBoxFrame::setDecorations(bool on)
{
    m_decorations = on;
    setAttribute(Qt::WA_OpaquePaintEvent, true); // we always fill the full rect
}

void TextBoxFrame::present(const QString &text, const QRectF &canvasBounds, int fontSize)
{
    QRect outer = canvasBounds.toAlignedRect();
    if (m_decorations) {
        outer = outer.adjusted(-kPad, -kPad, kPad, kPad);
        outer.setWidth(qMax(outer.width(),  120 + 2 * kPad));
        outer.setHeight(qMax(outer.height(),  20 + 2 * kPad));
    }

    // Expand height if the text doesn't fit at the given font size.
    // QFontMetrics gives us the wrapped text height for the inner width.
    {
        QFont f; f.setPixelSize(qMax(8, fontSize));
        QFontMetrics fm(f);
        const int innerW = qMax(1, outer.width() - (m_decorations ? 2 * kPad : 0) - 8);
        const QRect needed = fm.boundingRect(QRect(0, 0, innerW, 9999),
                                              Qt::TextWordWrap | Qt::AlignLeft, text);
        const int minOuterH = needed.height() + (m_decorations ? 2 * kPad : 0) + 8;
        if (outer.height() < minOuterH)
            outer.setHeight(minOuterH);
    }

    setGeometry(outer);
    m_editor->setGeometry(innerRect());
    m_editor->present(text, fontSize);  // sets text/style/show — setFocus skipped until parent visible

    raise();
    show();     // TextBoxFrame + InlineEditor child both become visible
    update();

    // Suppress the spurious immediate focusOut that fires right after setFocus()
    // (a pending mouse-release event from the original click steals focus back).
    // The 0ms timer re-gives focus once that pending event is consumed.
    m_editor->suppressNextFocusOut();
    m_editor->setFocus();
    QTimer::singleShot(0, this, [this]() {
        if (isVisible() && !m_editor->hasFocus())
            m_editor->setFocus();
    });
}

void TextBoxFrame::setFontSize(int pixelFontSize) { m_editor->setFontSize(pixelFontSize); }
void TextBoxFrame::setForbiddenZones(const QList<QRect> &zones) { m_forbidden = zones; }
void TextBoxFrame::resetCommitGuard() { m_editor->resetCommitGuard(); }

// ── Resize event ─────────────────────────────────────────────────────────────

void TextBoxFrame::resizeEvent(QResizeEvent *)
{
    m_editor->setGeometry(innerRect());
    if (isVisible())
        Q_EMIT boundsChanged(QRectF(innerRect()).translated(pos()));
}

// ── Hit testing ───────────────────────────────────────────────────────────────

TextBoxFrame::Handle TextBoxFrame::hitTest(const QPoint &p) const
{
    const int w = width(), h = height();
    const int z = kPad;   // hit-zone = kPad; InlineEditor starts at kPad so no overlap

    const bool L = p.x() <  z,       R = p.x() >= w - z;
    const bool T = p.y() <  z,       B = p.y() >= h - z;
    const bool mX = !L && !R,        mY = !T && !B;

    if (T && L)  return Handle::NW;
    if (T && R)  return Handle::NE;
    if (B && L)  return Handle::SW;
    if (B && R)  return Handle::SE;
    if (T && mX) return Handle::N;
    if (B && mX) return Handle::S;
    if (L && mY) return Handle::W;
    if (R && mY) return Handle::E;
    return Handle::None;
}

void TextBoxFrame::applyCursor(Handle h)
{
    switch (h) {
    case Handle::NW: case Handle::SE: setCursor(Qt::SizeFDiagCursor);  return;
    case Handle::NE: case Handle::SW: setCursor(Qt::SizeBDiagCursor);  return;
    case Handle::N:  case Handle::S:  setCursor(Qt::SizeVerCursor);    return;
    case Handle::W:  case Handle::E:  setCursor(Qt::SizeHorCursor);    return;
    default:                           unsetCursor();                    return;
    }
}

// ── Mouse events ─────────────────────────────────────────────────────────────

void TextBoxFrame::mousePressEvent(QMouseEvent *e)
{
    qWarning() << "[TBF] mousePressEvent pos=" << e->pos() << "hitTest=" << (int)hitTest(e->pos()) << "size=" << size();
    if (!m_decorations) { e->ignore(); return; }  // direct-edit: pass to InlineEditor
    if (e->button() == Qt::LeftButton) {
        const Handle h = hitTest(e->pos());
        if (h != Handle::None) {
            m_drag         = h;
            m_dragOrigin   = e->globalPosition().toPoint();
            m_dragStartGeo = geometry();
            applyCursor(h);
            m_editor->suppressNextFocusOut();  // don't commit when handle steals focus
            e->accept();
            return;
        }
    }
    // Not on a handle — let InlineEditor child receive it naturally.
    e->ignore();
}

void TextBoxFrame::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_decorations || m_drag == Handle::None) {
        if (m_decorations) applyCursor(hitTest(e->pos()));
        e->ignore();
        return;
    }

    const QPoint d  = e->globalPosition().toPoint() - m_dragOrigin;
    QRect geo       = m_dragStartGeo;
    const int minW  = 40 + 2 * kPad;
    const int minH  = 20 + 2 * kPad;

    if (m_drag == Handle::W || m_drag == Handle::NW || m_drag == Handle::SW)
        geo.setLeft(qMin(geo.left() + d.x(), geo.right() - minW));
    if (m_drag == Handle::E || m_drag == Handle::NE || m_drag == Handle::SE)
        geo.setRight(qMax(geo.right() + d.x(), geo.left() + minW));
    if (m_drag == Handle::N || m_drag == Handle::NW || m_drag == Handle::NE)
        geo.setTop(qMin(geo.top() + d.y(), geo.bottom() - minH));
    if (m_drag == Handle::S || m_drag == Handle::SW || m_drag == Handle::SE)
        geo.setBottom(qMax(geo.bottom() + d.y(), geo.top() + minH));

    // Prevent overlapping other text zones: reject the drag if it would collide.
    const QRect inner = geo.adjusted(kPad, kPad, -kPad, -kPad);
    for (const QRect &fz : m_forbidden) {
        if (inner.intersects(fz)) {
            e->accept();
            return;   // keep current geometry unchanged
        }
    }

    setGeometry(geo);
    e->accept();
}

void TextBoxFrame::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_drag != Handle::None) {
        m_drag = Handle::None;
        unsetCursor();
        e->accept();
        // Same pattern as present(): suppress the immediate focusOut from setFocus(),
        // then re-give focus after the event loop drains the pending mouse-release event.
        m_editor->suppressNextFocusOut();
        m_editor->setFocus();
        QTimer::singleShot(0, this, [this]() {
            if (isVisible() && !m_editor->hasFocus())
                m_editor->setFocus();
        });
        return;
    }
    e->ignore();
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void TextBoxFrame::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Always fill white — covers original page text regardless of mode.
    p.fillRect(rect(), Qt::white);

    // Direct-edit mode: no border, no handles — just a plain white editing area.
    if (!m_decorations) return;

    // Dashed blue border inset by half the handle size.
    const int h  = kH / 2;
    const QRect border = rect().adjusted(h, h, -h, -h);
    QPen pen(QColor(0x3B, 0x82, 0xF6), 2, Qt::CustomDashLine);
    pen.setDashPattern({4.0, 3.0});
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(border);

    // 8 solid blue handles at corners and edge midpoints.
    const int hw = kH;
    const int W  = width()  - hw;
    const int H  = height() - hw;
    const QPoint pts[] = {
        {0,    0},   {W/2, 0},  {W, 0},
        {0,    H/2},             {W, H/2},
        {0,    H},   {W/2, H},  {W, H},
    };
    p.setPen(QPen(QColor(0x3B, 0x82, 0xF6), 1));
    p.setBrush(QColor(0x3B, 0x82, 0xF6));
    for (const QPoint &pt : pts)
        p.drawRect(QRect(pt, QSize(hw, hw)));
}
