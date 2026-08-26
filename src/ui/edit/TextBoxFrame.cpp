#include "ui/edit/TextBoxFrame.hpp"
#include "ui/edit/InlineEditor.hpp"

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
        if (!isVisible()) return;
        growToFitText();
        layoutEditor();
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

void TextBoxFrame::setFontSize(qreal px)
{
    m_editor->setFontSizeF(px);
    growToFitText();
}

void TextBoxFrame::setBoxProperties(const TextBoxProperties &properties, qreal scale)
{
    m_box = properties;
    m_scale = qMax<qreal>(0.01, scale);
    m_autoHeight = properties.autoHeight;
    m_editor->setBoxProperties(properties, m_scale);
    layoutEditor();
    if (m_autoHeight) growToFitText();
    update();
}

void TextBoxFrame::setGrowHorizontal(bool on)
{
    m_growHorizontal = on;
    // A single-line edit must not wrap, at any zoom. Font sizes are whole
    // pixels, so the text is relatively wider than its box at low zoom and the
    // line broke in two — which grew the frame to two lines and, on commit,
    // painted a line break that is not in the document. growToFitText widens
    // the frame for these edits instead.
    m_editor->setLineWrapMode(on ? QTextEdit::NoWrap : QTextEdit::WidgetWidth);
}

void TextBoxFrame::growToFitText()
{
    int newW = width();

    // Single-line edits: grow the WIDTH while typing (up to the page edge)
    // so the line extends instead of wrapping — a wrap here is committed
    // into the document as a bogus paragraph break.
    if (m_growHorizontal) {
        const QFontMetricsF fm(m_editor->styledFont(m_editor->fontPixelSizeF()));
        qreal longest = 0.0;
        const QStringList lines = m_editor->toPlainText().split(u'\n');
        for (const QString &ln : lines)
            longest = qMax(longest, fm.horizontalAdvance(ln));
        const int neededW = qCeil(longest) + 3
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
    int newH = height();
    if (m_autoHeight)
        newH = qMax(height(), needed + (m_decorations ? 2 * kPad : 0) + 2);
    // Never grow beyond the page — a runaway height (bad detection, huge
    // font) must not blow the frame across the whole viewport.
    if (!m_pageRect.isNull())
        newH = qMin(newH, m_pageRect.bottom() - y() + 1);

    if (newW != width() || newH != height())
        resize(newW, newH);   // resizeEvent → boundsChanged updates the
                              // edit bounds in DocumentView
}

int TextBoxFrame::minInnerHeight(qreal fontPixelSize)
{
    return qMax(4, qCeil(fontPixelSize) + 2);
}

void TextBoxFrame::repositionForZoom(const QRectF &canvasBounds, qreal px,
                                     const TextBoxProperties &box, qreal scale)
{
    m_presenting = true;
    m_box        = box;
    m_scale      = qMax<qreal>(0.01, scale);
    m_autoHeight = box.autoHeight;
    m_editor->setBoxProperties(box, m_scale);

    QRect outer = canvasBounds.toAlignedRect();
    if (m_decorations)
        outer = outer.adjusted(-kPad, -kPad, kPad, kPad);
    outer.setWidth(qMax(outer.width(),  m_minInnerW + 2 * kPad));
    outer.setHeight(qMax(outer.height(), minInnerHeight(px) + 2 * kPad));

    // m_presenting suppresses boundsChanged for the WHOLE reposition, growth
    // included. A zoom must not redefine what is being edited: the frame's
    // minimum size, its padding and the growth headroom are fixed PIXEL
    // amounts, so feeding the grown geometry back as PDF points made the
    // tracked bounds bigger at every zoom step (worst at low zoom, where a
    // pixel is worth more points) until the box covered half the page — and
    // it marked an untouched edit as changed. The bounds are already known in
    // document space; here the frame only re-renders them at a new scale.
    setGeometry(outer);
    layoutEditor();
    m_editor->setFontSizeF(px);
    growToFitText();   // keep all text visible at the new zoom level
    m_presenting = false;
    update();
}
void TextBoxFrame::setTextAnchor(bool valid, const QPointF &penOffsetPt)
{
    m_hasAnchor = valid;
    m_anchorPt  = penOffsetPt;
}

void TextBoxFrame::layoutEditor()
{
    QRect r = innerRect();
    if (m_hasAnchor && qFuzzyIsNull(m_box.paddingPt)
            && m_box.verticalAlign == TextBoxProperties::VerticalAlign::Top) {
        r.translate(qRound(m_anchorPt.x() * m_scale),
                    qRound(m_anchorPt.y() * m_scale
                           - m_editor->firstBaselineOffset()));
    }
    m_editor->setGeometry(r);
}

void TextBoxFrame::setForbiddenZones(const QList<QRect> &z) { m_forbidden = z; }
void TextBoxFrame::setPageRect(const QRect &r) { m_pageRect = r; }
void TextBoxFrame::resetCommitGuard()      { m_editor->resetCommitGuard(); }
QString TextBoxFrame::currentText() const  { return m_editor->laidOutText(); }

void TextBoxFrame::present(const QString &text, const QRectF &canvasBounds, qreal fontSize,
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
        outer.setHeight(qMax(outer.height(), minInnerHeight(fontSize) + 2 * kPad));
    }

    // Guard boundsChanged during present() — the outer rect expands by kPad on all
    // sides, so innerRect() would give slightly different PDF coords than the true
    // block bounds. DocumentView set m_activeEditBounds from block.pdfBounds directly;
    // don't overwrite it with a canvas-rounded value here.
    m_presenting = true;
    setGeometry(outer);
    layoutEditor();
    m_editor->present(text, fontSize, color, fontFamily, bold, italic);

    raise();
    show();
    update();
    m_presenting = false;

    // If the text needs more room than the detected bounds (e.g. a paragraph
    // taller than its rect), grow now — no text may open hidden. Emits
    // boundsChanged so DocumentView tracks the grown edit area.
    growToFitText();
    layoutEditor();

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
    layoutEditor();
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
    const QRect in = innerRect();
    if (in.contains(p)) return Handle::None;

    const auto nearTo = [](int a, int b) { return qAbs(a - b) <= kH; };
    const bool hL  = nearTo(p.x(), in.left()),     hR = nearTo(p.x(), in.right());
    const bool hT  = nearTo(p.y(), in.top()),      hB = nearTo(p.y(), in.bottom());
    const bool hMX = nearTo(p.x(), in.center().x());
    const bool hMY = nearTo(p.y(), in.center().y());

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
    const int minW  = m_minInnerW + 2 * kPad;
    const int minH  = minInnerHeight(m_editor->fontPixelSizeF()) + 2 * kPad;

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
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF box = innerRect();
    p.save();
    p.setOpacity(qBound(0.0, m_box.opacity, 1.0));
    if (m_box.backgroundEnabled) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_box.backgroundColor);
        const qreal radius = qMax(0.0, m_box.cornerRadiusPt * m_scale);
        p.drawRoundedRect(box, radius, radius);
    }
    if (m_box.borderEnabled) {
        Qt::PenStyle style = Qt::SolidLine;
        if (m_box.borderStyle == TextBoxProperties::BorderStyle::Dashed)
            style = Qt::DashLine;
        else if (m_box.borderStyle == TextBoxProperties::BorderStyle::Dotted)
            style = Qt::DotLine;
        QPen boxPen(m_box.borderColor,
                    qMax(0.5, m_box.borderWidthPt * m_scale), style);
        p.setPen(boxPen);
        p.setBrush(Qt::NoBrush);
        const qreal radius = qMax(0.0, m_box.cornerRadiusPt * m_scale);
        p.drawRoundedRect(box.adjusted(boxPen.widthF() / 2.0,
                                      boxPen.widthF() / 2.0,
                                      -boxPen.widthF() / 2.0,
                                      -boxPen.widthF() / 2.0), radius, radius);
    }
    p.restore();

    if (!m_decorations) return;

    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect in = innerRect();

    // Dashed blue border
    QPen pen(QColor(0x3B, 0x82, 0xF6), 2, Qt::CustomDashLine);
    pen.setDashPattern({4.0, 3.0});
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(in.adjusted(-1, -1, 1, 1));

    const int hw = kH, half = hw / 2;
    const int l = in.left() - half,  r  = in.right()  - half;
    const int t = in.top()  - half,  b  = in.bottom() - half;
    const int mx = in.center().x() - half, my = in.center().y() - half;
    p.setPen(QPen(QColor(0x3B, 0x82, 0xF6), 1));
    p.setBrush(QColor(0x3B, 0x82, 0xF6));
    for (const QPoint &at : { QPoint(l, t), QPoint(mx, t), QPoint(r, t),
                              QPoint(l, my),                QPoint(r, my),
                              QPoint(l, b), QPoint(mx, b), QPoint(r, b) })
        p.drawRect(QRect(at, QSize(hw, hw)));
}
