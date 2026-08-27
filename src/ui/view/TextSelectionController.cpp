#include "ui/view/TextSelectionController.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/PdfBackend.hpp"
#endif

#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QStringList>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>

TextSelectionController::TextSelectionController(PageCanvas *canvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
{}

#ifdef HAVE_PDF_RENDERING
void TextSelectionController::setSource(PdfRenderer *renderer, const DocumentSource *source)
{
    m_renderer = renderer;
    m_src      = source;
}
#endif

// ── Mouse handling ────────────────────────────────────────────────────────────

void TextSelectionController::handlePress(const QPoint &canvasPos)
{
    // The anchor lives in canvas coords so scrolling during the drag does not
    // shift the selection start.
    clear();
    m_dragStart = canvasPos;
    m_tracking  = true;
    m_dragging  = false;
    Q_EMIT focusRequested();   // enables Ctrl+C on the view
}

bool TextSelectionController::handleMove(const QPoint &canvasPos)
{
    if (!m_tracking) return false;
    if (!m_dragging && (canvasPos - m_dragStart).manhattanLength() > 4)
        m_dragging = true;
    if (!m_dragging) return false;
    updateSelection(m_dragStart, canvasPos);
    return true;
}

bool TextSelectionController::handleRelease()
{
    if (!m_tracking) return false;
    m_tracking = false;
    // A plain click (no drag) just clears the previous marking.
    if (!m_dragging) return false;
    m_dragging = false;
    return true;
}

// ── Selection geometry ────────────────────────────────────────────────────────

bool TextSelectionController::anchorAt(const QPoint &canvasPos, int *page,
                                       QPointF *pdfPt) const
{
#ifdef HAVE_PDF_RENDERING
    const int labels = m_canvas->pageLabelCount();
    if (!m_renderer || labels == 0) return false;

    // Exact hit first, otherwise the vertically nearest page — dragging into a
    // margin or past the last page must still extend the selection.
    int    best     = -1;
    int    bestDist = std::numeric_limits<int>::max();
    for (int i = 0; i < labels; ++i) {
        const QLabel *lbl = m_canvas->pageLabel(i);
        if (!lbl) continue;
        const QRect g = lbl->geometry();
        if (g.contains(canvasPos)) { best = i; bestDist = 0; break; }
        const int dy = qMax(0, qMax(g.top() - canvasPos.y(), canvasPos.y() - g.bottom()));
        if (dy < bestDist) { bestDist = dy; best = i; }
    }
    if (best < 0) return false;

    const QRect  g     = m_canvas->pageLabel(best)->geometry();
    const qreal  scale = m_canvas->screenScale();
    const QPoint clamped(qBound(g.left(), canvasPos.x(), g.right()),
                         qBound(g.top(),  canvasPos.y(), g.bottom()));
    *page  = best;
    *pdfPt = QPointF(clamped - g.topLeft()) / scale;
    return true;
#else
    Q_UNUSED(canvasPos) Q_UNUSED(page) Q_UNUSED(pdfPt)
    return false;
#endif
}



void TextSelectionController::updateSelection(const QPoint &canvasFrom,
                                              const QPoint &canvasTo)
{
#ifdef HAVE_PDF_RENDERING
    m_parts.clear();

    int    pageA = -1, pageB = -1;
    QPointF ptA, ptB;
    if (!anchorAt(canvasFrom, &pageA, &ptA) || !anchorAt(canvasTo, &pageB, &ptB)) {
        updateOverlays();
        return;
    }

    // Normalise so the selection always runs forward through the document.
    if (pageB < pageA || (pageA == pageB
            && (ptB.y() < ptA.y() - 2.0
                || (std::abs(ptB.y() - ptA.y()) <= 2.0 && ptB.x() < ptA.x())))) {
        std::swap(pageA, pageB);
        std::swap(ptA, ptB);
    }

    const int pageCount = m_canvas->pageCount();
    auto *backend = m_src ? m_src->backend() : nullptr;
    if (!backend) { updateOverlays(); return; }

    for (int page = pageA; page <= pageB && page < pageCount; ++page) {
        // Die Randseiten werden an den Mausankern beschnitten, alles dazwischen
        // wird ganz genommen — ausgedrückt durch einen nicht gesetzten Anker.
        const PdfBackend::Selection sel = backend->selectPage(
            page,
            page == pageA ? std::optional<QPointF>(ptA) : std::nullopt,
            page == pageB ? std::optional<QPointF>(ptB) : std::nullopt);

        if (sel.rects.isEmpty() && sel.text.isEmpty()) continue;
        Part part;
        part.page  = page;
        part.rects = sel.rects;
        part.text  = sel.text;
        m_parts.append(part);
    }

    updateOverlays();
#else
    Q_UNUSED(canvasFrom) Q_UNUSED(canvasTo)
#endif
}

void TextSelectionController::updateOverlays()
{
#ifdef HAVE_PDF_RENDERING
    // Highlights are plain child widgets of the canvas, transparent for mouse
    // events so a follow-up drag starts a new selection instead of hitting them.
    // A selection dragged across a whole document can produce thousands of line
    // rects; the copied text stays complete, only the highlight is capped.
    constexpr int kMaxOverlays = 600;
    int used = 0;
    const qreal scale = m_canvas->screenScale();
    for (const Part &part : m_parts) {
        const QLabel *lbl = m_canvas->pageLabel(part.page);
        if (!lbl) continue;
        if (used >= kMaxOverlays) break;
        for (const QRectF &r : part.rects) {
            if (used >= kMaxOverlays) break;
            const QRectF canvasRect(r.topLeft() * scale + QPointF(lbl->pos()),
                                    r.size() * scale);
            QWidget *w = nullptr;
            if (used < m_overlays.size()) {
                w = m_overlays[used];
            } else {
                w = new QWidget(m_canvas->canvasWidget());
                w->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                w->setStyleSheet(QStringLiteral(
                    "background-color: rgba(59, 130, 246, 90);"));
                m_overlays.append(w);
            }
            w->setGeometry(canvasRect.toAlignedRect());
            w->raise();
            w->show();
            ++used;
        }
    }
    for (int i = used; i < m_overlays.size(); ++i)
        m_overlays[i]->hide();
#endif
}

void TextSelectionController::relayout()
{
    updateOverlays();
}

void TextSelectionController::clear()
{
    if (m_parts.isEmpty() && m_overlays.isEmpty()) return;
    m_parts.clear();
    for (QWidget *w : m_overlays) w->hide();
}

QString TextSelectionController::selectedText() const
{
    QStringList parts;
    for (const Part &p : m_parts)
        if (!p.text.isEmpty()) parts << p.text;
    return parts.join(QStringLiteral("\n"));
}

QList<TextSelectionController::SelectionPart> TextSelectionController::selectedParts() const
{
    QList<SelectionPart> result;
    result.reserve(m_parts.size());
    for (const Part &part : m_parts)
        result.append({ part.page, part.rects });
    return result;
}

void TextSelectionController::copyToClipboard() const
{
    const QString text = selectedText();
    if (!text.isEmpty()) QApplication::clipboard()->setText(text);
}
