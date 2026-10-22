#include "ui/edit/EditController.hpp"

#include "ui/view/PageCanvas.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentSource.hpp"
#  include "engine/edit/ContentModel.hpp"
#  include "engine/render/PdfRenderer.hpp"
#  include "ui/edit/TextBoxFrame.hpp"
#  include "ui/view/ZoomController.hpp"

#  include <QApplication>
#  include <QClipboard>
#  include <QLabel>
#  include <QMimeData>
#  include <QStringList>
#endif

bool EditController::hasTextAt(const QPoint &canvasPos) const
{
#ifdef HAVE_PDF_RENDERING
    auto [page, label] = m_canvas->pageAtCanvasPos(canvasPos);
    if (page < 0 || !label) return false;
    const QPointF pdfPt = QPointF(canvasPos - label->pos()) / PdfRenderer::screenScale(m_zoom->zoom());
    EditSession::Edit edit;
    if (m_session->findEditAt(page, pdfPt, &edit)) return true;
    return m_src->contentProvider() && m_src->contentProvider()->itemAt(page, pdfPt).isValid();
#else
    Q_UNUSED(canvasPos)
    return false;
#endif
}

bool EditController::copyBoxAt(const QPoint &canvasPos)
{
#ifdef HAVE_PDF_RENDERING
    // Opened and closed unchanged within one event, so it never shows.
    openAt(canvasPos, canvasPos, false);
    if (activeEditPage < 0 || !m_frame->isVisible()) return false;
    m_frame->copyAll();
    cancel();
    return true;
#else
    Q_UNUSED(canvasPos)
    return false;
#endif
}

bool EditController::pasteBox(const QPoint &canvasPos)
{
#ifdef HAVE_PDF_RENDERING
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime || mime->text().trimmed().isEmpty()) return false;
    auto [page, label] = m_canvas->pageAtCanvasPos(canvasPos);
    if (page < 0) return false;
    // A box copied whole comes back as it was; other text gets a new box at
    // the spot, as wide as the box copied, else a third of the page.
    const SourcedText::Box box = SourcedText::box(mime, documentKey());
    if (!box.lines.isEmpty()) return pasteLaid(page, label, canvasPos, box);
    double widthPt = box.size.width();
    if (widthPt <= 0.0) widthPt = m_src->renderer()->pageSizePts(page).width() / 3.0;
    const qreal scale = PdfRenderer::screenScale(m_zoom->zoom());
    const QPoint scroll = -m_canvas->canvasWidget()->pos();
    createTextFrame(QRect(canvasPos - scroll,
                          QSize(qMax(31, qRound(widthPt * scale)), qMax(16, qRound(16 * scale)))));
    if (activeEditPage < 0 || !m_frame->isVisible()) return false;
    m_frame->paste();
    return true;
#else
    Q_UNUSED(canvasPos)
    return false;
#endif
}

#ifdef HAVE_PDF_RENDERING
bool EditController::pasteLaid(int page, const QLabel *label, const QPoint &canvasPos,
                               const SourcedText::Box &box)
{
    // Every character where it was in the box copied, as a laid box at the spot,
    // moved rather than cut where the page ends.
    const qreal scale = PdfRenderer::screenScale(m_zoom->zoom());
    const QSizeF pageSize = m_src->renderer()->pageSizePts(page);
    QRectF bounds(QPointF(canvasPos - label->pos()) / scale, box.size);
    bounds.moveLeft(qBound(0.0, bounds.left(), qMax(0.0, pageSize.width() - bounds.width())));
    bounds.moveTop(qBound(0.0, bounds.top(), qMax(0.0, pageSize.height() - bounds.height())));

    EditTarget t;
    t.label    = const_cast<QLabel *>(label);
    t.scale    = scale;
    t.pdfPt    = bounds.topLeft();
    t.editPage = t.sourcePage = page;
    t.block.page = page;
    t.bounds = t.originalBounds = bounds;
    t.box.bounds = bounds;
    t.fontSizePt = t.baseSizePt = box.fontSizePt > 0.0 ? box.fontSizePt : 12.0;
    t.color   = QColor(0x11, 0x11, 0x11);
    t.bgColor = Qt::white;
    QStringList texts;
    for (qsizetype i = 0; i < box.lines.size(); ++i) {
        TextLayout::OriginalLine line = box.lines.at(i);
        line.baseline += bounds.top();
        for (double &x : line.charX) x += bounds.left();
        line.endX += bounds.left();
        line.rect.translate(bounds.topLeft());
        t.framedLines.append(line);
        if (i > 0) t.framedSources.append(TextLayout::Source{});
        t.framedSources += box.sources.at(i);
        texts << line.text;
    }
    t.displayText = t.selectionIn = texts.join(QLatin1Char('\n'));
    t.selStart = t.selEnd = int(t.displayText.size());
    t.textOrigin = QPointF(t.framedLines.first().charX.first(), t.framedLines.first().baseline);
    t.hasOrigin  = true;
    t.laid       = true;
    openTarget(t);
    if (activeEditPage < 0 || !m_frame->isVisible()) return false;
    // A new box: closing it keeps it, also unchanged.
    activeEditInPlace = false;
    showLook();
    return true;
}
#endif
