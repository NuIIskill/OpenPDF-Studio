#include "TextBoxFrame.hpp"
#include "InlineEditor.hpp"

#include <QPainter>
#include <QPen>
#include <QMouseEvent>
#include <QTimer>
#include <QtMath>
#include <QFontMetricsF>

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
    // Typing must never push text out of sight — grow the box with content.
    connect(m_editor, &InlineEditor::changed, this, [this]() {
        if (isVisible()) growToFitText();
    });
}

QRect TextBoxFrame::innerRect() const
{
    return m_decorations ? rect().adjusted(kPad, kPad, -kPad, -kPad) : rect();
}

QRectF TextBoxFrame::innerCanvasRect() const
{
    return QRectF(innerRect()).translated(pos());
}

void TextBoxFrame::setDecorations(bool on) { m_decorations = on; }
void TextBoxFrame::setTextColor(const QColor &c) { m_editor->setColor(c); }
void TextBoxFrame::setTextFont(const QString &family, bool bold, bool italic)
{
    m_editor->setTextFont(family, bold, italic);
}

void TextBoxFrame::setFontSize(int px)
{
    m_editor->setFontSize(px);
    growToFitText();
}

void TextBoxFrame::growToFitText()
{
    int newW = width();

    // Single-line edits: grow the WIDTH while typing (up to the page edge)
    // so the line extends instead of wrapping — a wrap here is committed
    // into the document as a bogus paragraph break.
    if (m_growHorizontal) {
        const QFontMetricsF fm(m_editor->styledFont(m_editor->fontPixelSize()));
        qreal longest = 0.0;
        const QStringList lines = m_editor->toPlainText().split(u'\n');
        for (const QString &ln : lines)
            longest = qMax(longest, fm.horizontalAdvance(ln));
        const int neededW = qCeil(longest + fm.averageCharWidth() + 10)
                          + (m_decorations ? 2 * kPad : 0);
        if (neededW > newW) {
            newW = neededW;
            if (!m_pageRect.isNull())
                newW = qMin(newW, m_pageRect.right() - x() + 1);
        }
    }

    // The document wraps at the editor width, so its size() is the exact
    // space the text needs at that width.
    const int needed = qCeil(m_editor->document()->size().height());
    int newH = qMax(height(), needed + (m_decorations ? 2 * kPad : 0) + 2);
    // Never grow beyond the page — a runaway height (bad detection, huge
    // font) must not blow the frame across the whole viewport.
    if (!m_pageRect.isNull())
        newH = qMin(newH, m_pageRect.bottom() - y() + 1);

    if (newW != width() || newH != height())
        resize(newW, newH);   // resizeEvent → boundsChanged updates the
                              // edit bounds in DocumentView
}

void TextBoxFrame::repositionForZoom(const QRectF &canvasBounds, int px)
{
    QRect outer = canvasBounds.toAlignedRect();
    if (m_decorations)
        outer = outer.adjusted(-kPad, -kPad, kPad, kPad);
    outer.setWidth(qMax(outer.width(),  m_minInnerW + 2 * kPad));
    outer.setHeight(qMax(outer.height(),  20 + 2 * kPad));

    // m_presenting suppresses boundsChanged so DocumentView doesn't update
    // m_activeEditBounds from this programmatic reposition.
    m_presenting = true;
    setGeometry(outer);
    m_editor->setGeometry(innerRect());
    m_editor->setFontSize(px);
    m_presenting = false;
    growToFitText();   // keep all text visible at the new zoom level
    update();
}
void TextBoxFrame::setForbiddenZones(const QList<QRect> &z) { m_forbidden = z; }
void TextBoxFrame::setPageRect(const QRect &r) { m_pageRect = r; }
void TextBoxFrame::resetCommitGuard()      { m_editor->resetCommitGuard(); }
QString TextBoxFrame::currentText() const  { return m_editor->toPlainText(); }

void TextBoxFrame::present(const QString &text, const QRectF &canvasBounds, int fontSize,
                           const QColor &color, const QString &fontFamily,
                           bool bold, bool italic)
{
    // Empty (drag-created) boxes need room to type into; boxes over existing
    // text keep the text's width — inflating it would leak into the tracked
    // edit bounds and misplace the committed edit.
    m_minInnerW = text.trimmed().isEmpty() ? 120 : 24;

    QRect outer = canvasBounds.toAlignedRect();
    if (m_decorations) {
        outer = outer.adjusted(-kPad, -kPad, kPad, kPad);
        outer.setWidth(qMax(outer.width(),  m_minInnerW + 2 * kPad));
        outer.setHeight(qMax(outer.height(),  20 + 2 * kPad));
    }

    // Guard boundsChanged during present() — the outer rect expands by kPad on all
    // sides, so innerRect() would give slightly different PDF coords than the true
    // block bounds. DocumentView set m_activeEditBounds from block.pdfBounds directly;
    // don't overwrite it with a canvas-rounded value here.
    m_presenting = true;
    setGeometry(outer);
    m_editor->setGeometry(innerRect());
    m_editor->present(text, fontSize, color, fontFamily, bold, italic);

    raise();
    show();
    update();
    m_presenting = false;

    // If the text needs more room than the detected bounds (e.g. a paragraph
    // taller than its rect), grow now — no text may open hidden. Emits
    // boundsChanged so DocumentView tracks the grown edit area.
    growToFitText();

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

    // Moving is FREE across the whole canvas — the box may be dragged onto
    // any other page; DocumentView retargets the page under the box center
    // via boundsChanged. Only resizing stays clamped to the current page.
    if (!m_pageRect.isNull() && m_drag != Handle::Move) {
        geo = geo.intersected(m_pageRect).normalized();
        if (geo.width() < minW || geo.height() < minH)
            geo = m_dragStartGeo;  // refuse the resize if it would be too small
    }

    setGeometry(geo);
    e->accept();
}

void TextBoxFrame::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_drag != Handle::None) {
        // A 1-2 px jitter while clicking the border must not move the box —
        // an accidental offset commits the text slightly displaced.
        if (m_drag == Handle::Move
                && (geometry().topLeft() - m_dragStartGeo.topLeft())
                       .manhattanLength() <= 2)
            setGeometry(m_dragStartGeo);
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
