#include "ui/view/TextSelectionController.hpp"

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
#  ifdef HAVE_QT_PDF
void TextSelectionController::setSource(PdfRenderer *renderer, QPdfDocument *document)
{
    m_renderer = renderer;
    m_document = document;
    invalidateCaches();
}
#  elif defined(HAVE_POPPLER)
void TextSelectionController::setSource(PdfRenderer *renderer, Poppler::Document *document)
{
    m_renderer = renderer;
    m_document = document;
    invalidateCaches();
}
#  else
void TextSelectionController::setSource(PdfRenderer *renderer)
{
    m_renderer = renderer;
    invalidateCaches();
}
#  endif
#endif

void TextSelectionController::invalidateCaches()
{
#ifdef HAVE_QT_PDF
    m_lineRectCache.clear();
#endif
}

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

#ifdef HAVE_QT_PDF
const QList<QRectF> &TextSelectionController::pageLineRects(int page)
{
    auto it = m_lineRectCache.find(page);
    if (it != m_lineRectCache.end()) return it.value();

    QList<QRectF> lines;
    if (m_document) {
        const QPdfSelection all = m_document->getAllText(page);
        for (const QPolygonF &poly : all.bounds()) {
            const QRectF r = poly.boundingRect();
            if (!r.isEmpty()) lines.append(r);
        }
    }
    return m_lineRectCache.insert(page, lines).value();
}

namespace {
// Pulls an anchor onto the nearest text line and clamps it inside that line.
// Vertical distance dominates so a point in the right-hand margin snaps to the
// end of its own line, not to a horizontally closer line above or below.
static QPointF snapToTextLine(const QList<QRectF> &lines, const QPointF &pt)
{
    if (lines.isEmpty()) return pt;
    const QRectF *best = nullptr;
    double bestDist = std::numeric_limits<double>::max();
    for (const QRectF &r : lines) {
        const double dy = qMax(0.0, qMax(r.top()  - pt.y(), pt.y() - r.bottom()));
        const double dx = qMax(0.0, qMax(r.left() - pt.x(), pt.x() - r.right()));
        const double d  = dy * 8.0 + dx;
        if (d < bestDist) { bestDist = d; best = &r; }
    }
    return QPointF(qBound(best->left() + 0.5, pt.x(), best->right()  - 0.5),
                   qBound(best->top()  + 0.5, pt.y(), best->bottom() - 0.5));
}

// First / last line in reading order — used as the page-spanning anchors of a
// multi-page selection.
static QPointF lineFlowStart(const QList<QRectF> &lines)
{
    const QRectF *top = &lines.first();
    for (const QRectF &r : lines) if (r.center().y() < top->center().y()) top = &r;
    return QPointF(top->left() + 0.5, top->center().y());
}
static QPointF lineFlowEnd(const QList<QRectF> &lines)
{
    const QRectF *bot = &lines.first();
    for (const QRectF &r : lines) if (r.center().y() > bot->center().y()) bot = &r;
    return QPointF(bot->right() - 0.5, bot->center().y());
}
} // namespace
#endif

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_POPPLER)
namespace {
// Poppler has no flow-selection API — rebuild it from the word list: sort the
// words in reading order, then take everything between the two anchor words.
struct PopplerSelWord { QRectF bbox; QString text; bool spaceAfter; };

static bool popplerReadingOrderLess(const PopplerSelWord &a, const PopplerSelWord &b)
{
    const double tol = qMax(4.0, qMin(a.bbox.height(), b.bbox.height()) * 0.6);
    if (std::abs(a.bbox.center().y() - b.bbox.center().y()) > tol)
        return a.bbox.center().y() < b.bbox.center().y();
    return a.bbox.left() < b.bbox.left();
}

// Index of the word the anchor belongs to. Points before/after the text flow
// clamp to the first/last word so partial drags still select something.
static int popplerAnchorIndex(const std::vector<PopplerSelWord> &words,
                              const QPointF &pt, bool preferAfter)
{
    if (words.empty()) return -1;
    int    best     = -1;
    double bestDist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < words.size(); ++i) {
        const QRectF &b = words[i].bbox;
        if (b.contains(pt)) return static_cast<int>(i);
        const double dx = qMax(0.0, qMax(b.left() - pt.x(), pt.x() - b.right()));
        // Vertical distance dominates: the word on the pointer's line wins over
        // a horizontally closer word one line above or below.
        const double dy = qMax(0.0, qMax(b.top() - pt.y(), pt.y() - b.bottom()));
        const double d  = dy * 4.0 + dx;
        if (d < bestDist) { bestDist = d; best = static_cast<int>(i); }
    }
    if (best < 0) return -1;
    // The nearest word may sit on the wrong side of the anchor; nudge the index
    // so a drag started right of a word does not swallow that word.
    const QRectF &b = words[best].bbox;
    if (!b.contains(pt)) {
        if (preferAfter && pt.x() > b.right() && best + 1 < static_cast<int>(words.size())
                && std::abs(words[best + 1].bbox.center().y() - b.center().y()) < b.height())
            ++best;
        else if (!preferAfter && pt.x() < b.left() && best > 0
                && std::abs(words[best - 1].bbox.center().y() - b.center().y()) < b.height())
            --best;
    }
    return best;
}
} // namespace
#endif

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
    for (int page = pageA; page <= pageB && page < pageCount; ++page) {
        const QSizeF pageSize = m_renderer->pageSizePts(page);
        const QPointF start = (page == pageA) ? ptA : QPointF(0.0, 0.0);
        const QPointF end   = (page == pageB)
                            ? ptB
                            : QPointF(pageSize.width(), pageSize.height());

        Part part;
        part.page = page;

#  ifdef HAVE_QT_PDF
        if (!m_document) continue;
        // Pages fully inside the range select as a whole; the two end pages are
        // cut at the (snapped) mouse anchors.
        // QPdfSelection has no default constructor — build it in one step.
        const QPdfSelection sel = [&]() -> QPdfSelection {
            if (page != pageA && page != pageB)
                return m_document->getAllText(page);
            const QList<QRectF> &lines = pageLineRects(page);
            if (lines.isEmpty())               // no text on this page
                return m_document->getSelection(page, QPointF(), QPointF());
            const QPointF s = (page == pageA) ? snapToTextLine(lines, start)
                                              : lineFlowStart(lines);
            const QPointF e = (page == pageB) ? snapToTextLine(lines, end)
                                              : lineFlowEnd(lines);
            return m_document->getSelection(page, s, e);
        }();
        if (!sel.isValid()) continue;
        for (const QPolygonF &poly : sel.bounds()) {
            const QRectF r = poly.boundingRect();
            if (!r.isEmpty()) part.rects.append(r);
        }
        QString text = sel.text();
        // PDF non-characters Qt emits for discretionary hyphens / padding.
        text.replace(QChar(0xFFFE), QStringLiteral("-"));
        text.replace(QChar(0xFFFF), QString{});
        part.text = text;
#  elif defined(HAVE_POPPLER)
        if (!m_document) continue;
        std::vector<PopplerSelWord> words;
        try {
            auto popplerPage = m_document->page(page);
            if (!popplerPage) continue;
            for (const auto &tb : popplerPage->textList()) {
                const QRectF b = tb->boundingBox();
                if (!b.isEmpty())
                    words.push_back({ b, tb->text(), tb->hasSpaceAfter() });
            }
        } catch (...) { continue; }
        if (words.empty()) continue;
        std::sort(words.begin(), words.end(), popplerReadingOrderLess);

        const int first = (page == pageA) ? popplerAnchorIndex(words, start, true) : 0;
        const int last  = (page == pageB) ? popplerAnchorIndex(words, end, false)
                                          : static_cast<int>(words.size()) - 1;
        if (first < 0 || last < 0 || last < first) continue;

        QRectF lineRect;
        double lineY = 0.0;
        for (int i = first; i <= last; ++i) {
            const PopplerSelWord &w = words[i];
            const bool newLine = lineRect.isNull()
                              || std::abs(w.bbox.center().y() - lineY)
                                     > qMax(4.0, w.bbox.height() * 0.6);
            if (newLine) {
                if (!lineRect.isNull()) { part.rects.append(lineRect); part.text += QLatin1Char('\n'); }
                lineRect = w.bbox;
                lineY    = w.bbox.center().y();
            } else {
                lineRect = lineRect.united(w.bbox);
                if (words[i - 1].spaceAfter) part.text += QLatin1Char(' ');
            }
            part.text += w.text;
        }
        if (!lineRect.isNull()) part.rects.append(lineRect);
#  endif

        if (!part.rects.isEmpty() || !part.text.isEmpty())
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

void TextSelectionController::copyToClipboard() const
{
    const QString text = selectedText();
    if (!text.isEmpty()) QApplication::clipboard()->setText(text);
}
