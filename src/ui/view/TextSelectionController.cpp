#include "ui/view/TextSelectionController.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/PdfBackend.hpp"
#endif
#include "engine/edit/SourcedText.hpp"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
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

void TextSelectionController::handlePress(const QPoint &canvasPos, bool extend)
{
    m_dragStart = canvasPos;
    m_tracking  = true;
    Q_EMIT focusRequested();

    if (extend && hasSelection() && m_anchorPage >= 0) {
        m_dragging = true;
        updateSelection(canvasPos);
        return;
    }

    clear();
    m_dragging = false;
    if (!anchorAt(canvasPos, &m_anchorPage, &m_anchorPt)) m_anchorPage = -1;
}

bool TextSelectionController::handleMove(const QPoint &canvasPos)
{
    if (!m_tracking) return false;
    if (!m_dragging && (canvasPos - m_dragStart).manhattanLength() > 4)
        m_dragging = true;
    if (!m_dragging) return false;
    updateSelection(canvasPos);
    return true;
}

bool TextSelectionController::handleRelease()
{
    if (!m_tracking) return false;
    m_tracking = false;

    if (!m_dragging) return false;
    m_dragging = false;
    return true;
}

bool TextSelectionController::anchorAt(const QPoint &canvasPos, int *page,
                                       QPointF *pdfPt) const
{
#ifdef HAVE_PDF_RENDERING
    const int labels = m_canvas->pageLabelCount();
    if (!m_renderer || labels == 0) return false;

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

void TextSelectionController::updateSelection(const QPoint &canvasTo)
{
#ifdef HAVE_PDF_RENDERING
    m_parts.clear();

    int     pageA = m_anchorPage, pageB = -1;
    QPointF ptA   = m_anchorPt, ptB;
    if (pageA < 0 || !anchorAt(canvasTo, &pageB, &ptB)) {
        updateOverlays();
        return;
    }

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

        const PdfBackend::Selection sel = backend->selectPage(
            page,
            page == pageA ? std::optional<QPointF>(ptA) : std::nullopt,
            page == pageB ? std::optional<QPointF>(ptB) : std::nullopt);

        if (sel.rects.isEmpty() && sel.text.isEmpty()) continue;
        Part part;
        part.page  = page;
        part.rects = sel.rects;
        part.text  = sel.text;
        part.sources = sel.sources;
        part.lines   = sel.lines;
        m_parts.append(part);
    }

    updateOverlays();
#else
    Q_UNUSED(canvasTo)
#endif
}

void TextSelectionController::updateOverlays()
{
#ifdef HAVE_PDF_RENDERING

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
    if (text.isEmpty()) return;
    QList<TextLayout::Source> sources;
    for (const Part &p : m_parts) {
        if (p.text.isEmpty()) continue;
        if (!sources.isEmpty()) sources.append(TextLayout::Source{});
        sources += p.sources.size() == p.text.size()
                       ? p.sources : QList<TextLayout::Source>(p.text.size());
    }
    // Text of one page keeps where each character is set, so it is pasted as a box alike.
    SourcedText::Box box;
    const Part *only = nullptr;
    int filled = 0;
    for (const Part &p : m_parts)
        if (!p.text.isEmpty()) { only = &p; ++filled; }
    if (filled == 1) {
        QList<QList<TextLayout::Source>> lineSources;
        qsizetype at = 0;
        for (const TextLayout::OriginalLine &line : only->lines) {
            lineSources.append(only->sources.mid(at, line.text.size()));
            at += line.text.size() + 1;
        }
        if (at - 1 == only->sources.size())
            box = SourcedText::boxAround(only->lines, lineSources, 0.0);
    }
    auto *mime = new QMimeData;
    mime->setText(text);
    SourcedText::write(mime, SourcedText::documentKey(m_src), sources, box);
    QApplication::clipboard()->setMimeData(mime);
}
