#include "TextBoxFrame.hpp"
#include "InlineEditor.hpp"

#include <QPainter>
#include <QPen>
#include <QMouseEvent>
#include <QTimer>
#include <QFontMetrics>

// ── Construction ──────────────────────────────────────────────────────────────

TextBoxFrame::TextBoxFrame(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
    setMouseTracking(true);
    hide();

    m_editor = new InlineEditor(this);
    m_editor->setAutoFillBackground(false);
    m_editor->viewport()->setAutoFillBackground(false);
    m_editor->viewport()->setStyleSheet("background: transparent;");
    connect(m_editor, &InlineEditor::committed, this, &TextBoxFrame::committed);
    connect(m_editor, &InlineEditor::cancelled,  this, &TextBoxFrame::cancelled);
    connect(m_editor, &InlineEditor::changed,    this, &TextBoxFrame::changed);
}

QRect TextBoxFrame::innerRect() const
{
    return m_decorations ? rect().adjusted(kPad, kPad, -kPad, -kPad) : rect();
}

void TextBoxFrame::setDecorations(bool on) { m_decorations = on; }
void TextBoxFrame::setFontSize(int px)     { m_editor->setFontSize(px); }
void TextBoxFrame::setForbiddenZones(const QList<QRect> &z) { m_forbidden = z; }
void TextBoxFrame::setPageRect(const QRect &r) { m_pageRect = r; }
void TextBoxFrame::resetCommitGuard()      { m_editor->resetCommitGuard(); }
QString TextBoxFrame::currentText() const  { return m_editor->toPlainText(); }

void TextBoxFrame::present(const QString &text, const QRectF &canvasBounds, int fontSize,
                           const QColor &color)
{
    QRect outer = canvasBounds.toAlignedRect();
    if (m_decorations) {
        outer = outer.adjusted(-kPad, -kPad, kPad, kPad);
        outer.setWidth(qMax(outer.width(),  120 + 2 * kPad));
        outer.setHeight(qMax(outer.height(),  20 + 2 * kPad));
    }

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

    // Guard boundsChanged during present() — the outer rect expands by kPad on all
    // sides, so innerRect() would give slightly different PDF coords than the true
    // block bounds. DocumentView set m_activeEditBounds from block.pdfBounds directly;
    // don't overwrite it with a canvas-rounded value here.
    m_presenting = true;
    setGeometry(outer);
    m_editor->setGeometry(innerRect());
    m_editor->present(text, fontSize, color);

    raise();
    show();
    update();
    m_presenting = false;

    m_editor->suppressNextFocusOut();
    m_editor->setFocus();
    QTimer::singleShot(0, this, [this]() {
        if (isVisible() && !m_editor->hasFocus())
            m_editor->setFocus();
    });
}

// ── Resize / move events ──────────────────────────────────────────────────────

void TextBoxFrame::resizeEvent(QResizeEvent *)
{
    m_editor->setGeometry(innerRect());
    if (isVisible() && !m_presenting)
        Q_EMIT boundsChanged(QRectF(innerRect()).translated(pos()));
}

void TextBoxFrame::moveEvent(QMoveEvent *)
{
    if (isVisible() && !m_presenting)
        Q_EMIT boundsChanged(QRectF(innerRect()).translated(pos()));
}

// ── Hit testing ───────────────────────────────────────────────────────────────

TextBoxFrame::Handle TextBoxFrame::hitTest(const QPoint &p) const
{
    const int w = width(), h = height();

    // Interior: not on any kPad-wide border — clicks pass through to editor
    const bool onBorder = (p.x() < kPad || p.x() >= w - kPad ||
                           p.y() < kPad || p.y() >= h - kPad);
    if (!onBorder) return Handle::None;

    // Handle squares: kH × kH at corners and edge midpoints
    const bool hL  = p.x() < kH,               hR  = p.x() >= w - kH;
    const bool hT  = p.y() < kH,               hB  = p.y() >= h - kH;
    const bool hMX = (p.x() >= w/2 - kH/2 && p.x() < w/2 + kH/2);
    const bool hMY = (p.y() >= h/2 - kH/2 && p.y() < h/2 + kH/2);

    if (hT && hL)  return Handle::NW;
    if (hT && hR)  return Handle::NE;
    if (hB && hL)  return Handle::SW;
    if (hB && hR)  return Handle::SE;
    if (hT && hMX) return Handle::N;
    if (hB && hMX) return Handle::S;
    if (hL && hMY) return Handle::W;
    if (hR && hMY) return Handle::E;

    // Rest of border between handles — drag to move the box
    return Handle::Move;
}

void TextBoxFrame::applyCursor(Handle h)
{
    switch (h) {
    case Handle::NW: case Handle::SE: setCursor(Qt::SizeFDiagCursor); return;
    case Handle::NE: case Handle::SW: setCursor(Qt::SizeBDiagCursor); return;
    case Handle::N:  case Handle::S:  setCursor(Qt::SizeVerCursor);   return;
    case Handle::W:  case Handle::E:  setCursor(Qt::SizeHorCursor);   return;
    case Handle::Move:                setCursor(Qt::SizeAllCursor);   return;
    default:                           unsetCursor();                   return;
    }
}

// ── Mouse events ─────────────────────────────────────────────────────────────

void TextBoxFrame::mousePressEvent(QMouseEvent *e)
{
    if (!m_decorations) { e->ignore(); return; }
    if (e->button() == Qt::LeftButton) {
        const Handle h = hitTest(e->pos());
        if (h != Handle::None) {
            m_drag         = h;
            m_dragOrigin   = e->globalPosition().toPoint();
            m_dragStartGeo = geometry();
            applyCursor(h);
            m_editor->setDragMode(true);
            m_editor->suppressNextFocusOut();
            e->accept();
            return;
        }
    }
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

    if (m_drag == Handle::Move) {
        geo.translate(d);
    } else {
        // Horizontal adjustments
        if (m_drag == Handle::W || m_drag == Handle::NW || m_drag == Handle::SW)
            geo.setLeft(qMin(geo.left() + d.x(), geo.right() - minW));
        if (m_drag == Handle::E || m_drag == Handle::NE || m_drag == Handle::SE)
            geo.setRight(qMax(geo.right() + d.x(), geo.left() + minW));
        // Vertical adjustments
        if (m_drag == Handle::N || m_drag == Handle::NW || m_drag == Handle::NE)
            geo.setTop(qMin(geo.top() + d.y(), geo.bottom() - minH));
        if (m_drag == Handle::S || m_drag == Handle::SW || m_drag == Handle::SE)
            geo.setBottom(qMax(geo.bottom() + d.y(), geo.top() + minH));
    }

    // Clamp to PDF page boundary so the box can't go outside the page
    if (!m_pageRect.isNull()) {
        if (m_drag == Handle::Move) {
            // Translate back so the box stays inside the page rect
            geo.moveLeft(qBound(m_pageRect.left(), geo.left(), m_pageRect.right() - geo.width()));
            geo.moveTop(qBound(m_pageRect.top(), geo.top(), m_pageRect.bottom() - geo.height()));
        } else {
            geo = geo.intersected(m_pageRect).normalized();
            if (geo.width() < minW || geo.height() < minH)
                geo = m_dragStartGeo;  // refuse the resize if it would be too small
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
        Q_EMIT dragEnded();
        m_editor->suppressNextFocusOut();
        m_editor->setFocus();
        QTimer::singleShot(0, this, [this]() {
            m_editor->setDragMode(false);
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
    if (!m_decorations) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int w = width(), h = height();

    // Dashed blue border
    QPen pen(QColor(0x3B, 0x82, 0xF6), 2, Qt::CustomDashLine);
    pen.setDashPattern({4.0, 3.0});
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(1, 1, -1, -1));

    // Resize handles: solid squares at all 8 resize positions
    const int hw = kH;
    p.setPen(QPen(QColor(0x3B, 0x82, 0xF6), 1));
    p.setBrush(QColor(0x3B, 0x82, 0xF6));
    // Top row: NW, N, NE
    p.drawRect(QRect(0,           0,      hw, hw));
    p.drawRect(QRect(w/2 - hw/2,  0,      hw, hw));
    p.drawRect(QRect(w - hw,      0,      hw, hw));
    // Middle row: W, E
    p.drawRect(QRect(0,     h/2 - hw/2, hw, hw));
    p.drawRect(QRect(w - hw, h/2 - hw/2, hw, hw));
    // Bottom row: SW, S, SE
    p.drawRect(QRect(0,           h - hw, hw, hw));
    p.drawRect(QRect(w/2 - hw/2, h - hw, hw, hw));
    p.drawRect(QRect(w - hw,     h - hw, hw, hw));
}
