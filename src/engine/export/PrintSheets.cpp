#include "engine/export/PrintSheets.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/render/PdfRenderer.hpp"

#include <QPainter>
#include <QtMath>

#ifdef HAVE_QT_PRINT
#  include <QPageLayout>
#  include <QPrinter>
#endif

namespace {

constexpr qreal kMaxRenderPixels = 60.0e6;
constexpr qreal kPosterOverlapPt = 10.0 / 25.4 * 72.0;

}

PrintSheets::PrintSheets(const DocumentExporter::Sources &src, const PrintJob &job)
    : m_src(src)
    , m_job(job)
{
    m_job.posterSheets = qMax(1, m_job.posterSheets);
    m_job.pagesPerSheet = m_job.posterSheets > 1 ? 1 : qMax(1, m_job.pagesPerSheet);
    QList<int> pages;
    for (int p : job.pages)
        if (p >= 0 && p < m_src.pageCount) pages.append(p);
    if (job.pages.isEmpty())
        for (int p = 0; p < m_src.pageCount; ++p) pages.append(p);
    m_job.pages = pages;
}

QSize PrintSheets::grid(int pagesPerSheet, bool landscapeSheet)
{
    QSize cells;
    switch (pagesPerSheet) {
    case 2:  cells = QSize(1, 2); break;
    case 4:  cells = QSize(2, 2); break;
    case 6:  cells = QSize(2, 3); break;
    case 9:  cells = QSize(3, 3); break;
    case 16: cells = QSize(4, 4); break;
    default: cells = QSize(1, 1); break;
    }
    return landscapeSheet ? cells.transposed() : cells;
}

QSize PrintSheets::posterGrid(int sheets, bool landscapeSheet)
{
    return grid(sheets, !landscapeSheet);
}

int PrintSheets::sheetCount() const
{
    if (!m_src.renderer) return 0;
    if (m_job.posterSheets > 1) return int(m_job.pages.size()) * m_job.posterSheets;
    return (int(m_job.pages.size()) + m_job.pagesPerSheet - 1) / m_job.pagesPerSheet;
}

QList<int> PrintSheets::pagesOnSheet(int sheet) const
{
    if (sheet < 0 || sheet >= sheetCount()) return {};
    if (m_job.posterSheets > 1) return { m_job.pages.at(sheet / m_job.posterSheets) };
    return m_job.pages.mid(sheet * m_job.pagesPerSheet, m_job.pagesPerSheet);
}

QSizeF PrintSheets::pageSizePts(int page) const
{
    return m_src.renderer ? m_src.renderer->pageSizePts(page) : QSizeF();
}

void PrintSheets::drawPage(QPainter &painter, int page, const QRectF &area, const QRectF &clip,
                           qreal dpi, qreal maxRenderDpi, PrintJob::Scaling scaling) const
{
    const QSizeF pts = pageSizePts(page);
    if (pts.isEmpty()) return;

    const auto fitScale = [&area](const QSizeF &s) {
        return qMin(area.width() / s.width(), area.height() / s.height());
    };
    const bool rotate = fitScale(pts.transposed()) > fitScale(pts) * 1.05;
    const QSizeF shown = rotate ? pts.transposed() : pts;

    const qreal unitsPerPt = dpi / PdfRenderer::kPtsPerInch;
    qreal scale = fitScale(shown);
    if (scaling == PrintJob::Scaling::ShrinkOversize)
        scale = qMin(scale, unitsPerPt);
    else if (scaling == PrintJob::Scaling::ActualSize)
        scale = unitsPerPt;

    const QSizeF drawn = shown * scale;
    const QRectF dest(area.x() + (area.width()  - drawn.width())  / 2.0,
                      area.y() + (area.height() - drawn.height()) / 2.0,
                      drawn.width(), drawn.height());

    qreal renderScale = scale * qMin(1.0, maxRenderDpi / dpi);
    renderScale = qMin(renderScale, qSqrt(kMaxRenderPixels / (pts.width() * pts.height())));
    if (m_cache.page != page || !qFuzzyCompare(m_cache.scale, renderScale)) {
        QImage image = m_src.renderer->renderPage(page, renderScale, m_src.session);
        if (m_job.grayscale && !image.isNull())
            image = image.convertToFormat(QImage::Format_Grayscale8);
        m_cache = { page, renderScale, image };
    }
    if (m_cache.image.isNull()) return;

    painter.save();
    painter.setClipRect(clip);
    if (rotate) {
        painter.translate(dest.center());
        painter.rotate(90);
        painter.drawImage(QRectF(-dest.height() / 2.0, -dest.width() / 2.0,
                                 dest.height(), dest.width()), m_cache.image);
    } else {
        painter.drawImage(dest, m_cache.image);
    }
    painter.restore();
}

void PrintSheets::paintPosterTile(QPainter &painter, int sheet, const QRectF &printable,
                                  qreal dpi, qreal maxRenderDpi) const
{
    const QSize tiles = posterGrid(m_job.posterSheets, printable.width() > printable.height());
    const int perPage = tiles.width() * tiles.height();
    const int tile = sheet % perPage;
    const int column = tile % tiles.width();
    const int row = tile / tiles.width();
    const qreal overlap = kPosterOverlapPt * dpi / PdfRenderer::kPtsPerInch;

    const QSizeF poster(tiles.width()  * printable.width()  - (tiles.width()  - 1) * overlap,
                        tiles.height() * printable.height() - (tiles.height() - 1) * overlap);
    const QPointF origin = printable.topLeft()
        - QPointF(column * (printable.width() - overlap), row * (printable.height() - overlap));
    drawPage(painter, pagesOnSheet(sheet).value(0), QRectF(origin, poster), printable, dpi,
             maxRenderDpi, PrintJob::Scaling::Fit);

    if (!m_job.cutMarks) return;
    painter.save();
    QPen pen(QColor(110, 110, 110), qMax(1.0, 0.4 * dpi / PdfRenderer::kPtsPerInch), Qt::DashLine);
    painter.setPen(pen);
    QList<qreal> xs, ys;
    if (column > 0)                 xs.append(printable.left() + overlap);
    if (column < tiles.width() - 1) xs.append(printable.right() - overlap);
    if (row > 0)                    ys.append(printable.top() + overlap);
    if (row < tiles.height() - 1)   ys.append(printable.bottom() - overlap);
    for (qreal x : xs)
        painter.drawLine(QPointF(x, printable.top()), QPointF(x, printable.bottom()));
    for (qreal y : ys)
        painter.drawLine(QPointF(printable.left(), y), QPointF(printable.right(), y));
    painter.restore();
}

void PrintSheets::paint(QPainter &painter, int sheet, const QRectF &printable, qreal dpi,
                        qreal maxRenderDpi) const
{
    const QList<int> pages = pagesOnSheet(sheet);
    if (pages.isEmpty() || printable.isEmpty() || dpi <= 0.0) return;
    if (m_job.posterSheets > 1) {
        paintPosterTile(painter, sheet, printable, dpi, maxRenderDpi);
        return;
    }

    const QSize cells = grid(m_job.pagesPerSheet, printable.width() > printable.height());
    const qreal gap = m_job.pagesPerSheet > 1
        ? qMin(printable.width(), printable.height()) * 0.02 : 0.0;
    const qreal cellW = (printable.width()  - gap * (cells.width()  - 1)) / cells.width();
    const qreal cellH = (printable.height() - gap * (cells.height() - 1)) / cells.height();
    for (int i = 0; i < pages.size(); ++i) {
        const QRectF cell(printable.x() + (i % cells.width()) * (cellW + gap),
                          printable.y() + (i / cells.width()) * (cellH + gap),
                          cellW, cellH);
        drawPage(painter, pages.at(i), cell, cell, dpi, maxRenderDpi, m_job.scaling);
    }
}

QImage PrintSheets::preview(int sheet, const QSizeF &paperPts, const QRectF &printablePts,
                            qreal pixelsPerPt) const
{
    const QSize size = (paperPts * pixelsPerPt).toSize();
    if (size.isEmpty()) return {};
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF printable(printablePts.topLeft() * pixelsPerPt,
                           printablePts.size() * pixelsPerPt);
    const qreal dpi = pixelsPerPt * PdfRenderer::kPtsPerInch;
    paint(painter, sheet, printable, dpi, dpi);
    return image;
}

#ifdef HAVE_QT_PRINT
bool PrintSheets::print(QPrinter *printer) const
{
    const int sheets = sheetCount();
    if (!printer || sheets <= 0) return false;

    printer->setFullPage(true);
    const qreal dpi = printer->resolution();
    const qreal unitsPerPt = dpi / PdfRenderer::kPtsPerInch;
    QPageLayout layout = printer->pageLayout();
    layout.setUnits(QPageLayout::Point);
    const QRectF printablePts = layout.fullRect().marginsRemoved(layout.minimumMargins());
    const QRectF printable(printablePts.topLeft() * unitsPerPt, printablePts.size() * unitsPerPt);

    const int copies = printer->supportsMultipleCopies() ? 1 : qMax(1, printer->copyCount());
    const bool padForBack = printer->duplex() != QPrinter::DuplexNone && sheets % 2 == 1;
    QList<int> order;
    if (printer->collateCopies() || copies == 1) {
        for (int c = 0; c < copies; ++c) {
            if (c > 0 && padForBack) order.append(-1);
            for (int s = 0; s < sheets; ++s) order.append(s);
        }
    } else {
        for (int s = 0; s < sheets; ++s)
            for (int c = 0; c < copies; ++c) order.append(s);
    }

    QPainter painter;
    if (!painter.begin(printer)) return false;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (int i = 0; i < order.size(); ++i) {
        if (i > 0 && !printer->newPage()) {
            painter.end();
            return false;
        }
        if (order.at(i) >= 0)
            paint(painter, order.at(i), printable, dpi, m_job.maxRenderDpi);
    }
    painter.end();
    return true;
}
#endif

#endif
