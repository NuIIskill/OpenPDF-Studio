#include "engine/edit/DocumentExporter.hpp"
#include "engine/document/PdfBackend.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/edit/ContentMap.hpp"
#  include "engine/edit/ContentModel.hpp"
#  include "engine/edit/DocxLayout.hpp"
#  include "engine/edit/EditSession.hpp"
#  include "engine/ocr/OcrEngine.hpp"
#  include "engine/render/PdfRenderer.hpp"
#  ifdef HAVE_QT_PRINT
#    include <QPrinter>
#  endif
#endif

#include <QDir>
#include <QFileInfo>
#include <QLineF>
#include <QPainter>

#include <algorithm>

#ifdef HAVE_PDF_RENDERING
namespace {

int colorDistance(const QColor &a, const QColor &b)
{
    return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green())
         + qAbs(a.blue() - b.blue());
}

QColor sampledBackground(const QImage &image, const QRectF &pdfRect, qreal scale,
                         const QColor &textColor, const QColor &fallback)
{
    const QRectF px(pdfRect.topLeft() * scale, pdfRect.size() * scale);
    const int left   = qFloor(px.left()) - 2;
    const int right  = qCeil(px.right()) + 2;
    const int top    = qFloor(px.top()) - 2;
    const int bottom = qCeil(px.bottom()) + 2;
    const int midX   = qRound(px.center().x());
    const int midY   = qRound(px.center().y());
    const QPoint probes[] = {
        { left, top }, { midX, top }, { right, top },
        { left, midY },               { right, midY },
        { left, bottom }, { midX, bottom }, { right, bottom }
    };

    struct Bucket { QColor color; int count; };
    QList<Bucket> buckets;
    for (const QPoint &probe : probes) {
        const int x = qBound(0, probe.x(), image.width() - 1);
        const int y = qBound(0, probe.y(), image.height() - 1);
        const QColor sample = image.pixelColor(x, y);

        if (textColor.isValid() && colorDistance(sample, textColor) < 80)
            continue;
        int nearest = -1;
        for (int i = 0; i < buckets.size(); ++i) {
            if (colorDistance(sample, buckets[i].color) < 36) {
                nearest = i;
                break;
            }
        }
        if (nearest >= 0)
            ++buckets[nearest].count;
        else
            buckets.append({ sample, 1 });
    }
    if (buckets.isEmpty()) return fallback.isValid() ? fallback : QColor(Qt::white);

    const Bucket *best = &buckets.first();
    for (const Bucket &bucket : buckets) {
        if (bucket.count > best->count)
            best = &bucket;
        else if (bucket.count == best->count && fallback.isValid()
                 && colorDistance(bucket.color, fallback)
                        < colorDistance(best->color, fallback))
            best = &bucket;
    }
    return best->color;
}

const ContentItem *styleDonor(const QList<ContentItem> &detected,
                              const QRectF &rect)
{
    const ContentItem *best = nullptr;
    double bestScore = 0.0;
    for (const ContentItem &candidate : detected) {
        if (!candidate.isTextual()) continue;
        const QRectF hit = candidate.bounds.intersected(rect);
        if (hit.isEmpty()) continue;

        const double vShare = hit.height() / qMax(1e-6, rect.height());
        const double hShare = hit.width()  / qMax(1e-6, rect.width());
        const double score  = vShare * 0.65 + hShare * 0.35;
        if (score > bestScore) {
            bestScore = score;
            best = &candidate;
        }
    }

    return bestScore >= 0.5 ? best : nullptr;
}

QList<QRectF> mergeGlyphBoxes(QList<QRectF> boxes)
{
    std::sort(boxes.begin(), boxes.end(), [](const QRectF &a, const QRectF &b) {
        if (std::abs(a.center().y() - b.center().y()) > 1.5)
            return a.center().y() < b.center().y();
        return a.left() < b.left();
    });

    QList<double> widths;
    for (const QRectF &r : boxes) widths.append(r.width());
    std::sort(widths.begin(), widths.end());
    const double advance = widths.isEmpty() ? 4.0 : widths[widths.size() / 2];

    QList<QRectF> runs;
    for (const QRectF &box : boxes) {
        if (!runs.isEmpty()) {
            QRectF &current = runs.last();
            const double lineH = qMax(current.height(), box.height());
            const bool sameLine =
                std::abs(current.center().y() - box.center().y()) <= lineH * 0.6;
            if (sameLine && box.left() - current.right() <= advance * 0.9) {
                current = current.united(box);
                continue;
            }
        }
        runs.append(box);
    }
    return runs;
}

}
#endif

#ifdef HAVE_PDF_RENDERING

QImage DocumentExporter::eraseTextRuns(const QImage &original,
                                       const QList<ContentItem> &items,
                                       int page, qreal scale) const
{
    QImage erased = original;
    if (erased.isNull()) return erased;

    QPainter painter(&erased);
    painter.setRenderHint(QPainter::Antialiasing, false);
    if (m_src.session) {
        for (const EditSession::ImageEdit &edit : m_src.session->imageEdits()) {
            if (edit.page != page || edit.image.isNull()) continue;
            painter.drawImage(QRectF(edit.pdfBounds.topLeft() * scale,
                                     edit.pdfBounds.size() * scale),
                              edit.image);
        }
    }
    for (const ContentItem &item : items) {
        if (!item.isTextual() || item.text.trimmed().isEmpty()) continue;
        QList<QRectF> eraseRects;
        if (m_src.backend)
            eraseRects = m_src.backend->glyphRects(page, item.bounds, {});
        if (eraseRects.isEmpty()) eraseRects.append(item.bounds);

        QColor itemBg = item.bgColor;
        if (!itemBg.isValid()) {
            const QPointF probePt((item.bounds.left() - 2.0) * scale,
                                  item.bounds.center().y() * scale);
            const int x = qBound(0, qRound(probePt.x()),
                                 erased.width() - 1);
            const int y = qBound(0, qRound(probePt.y()),
                                 erased.height() - 1);
            itemBg = erased.pixelColor(x, y);

            if (itemBg.lightness() < 45 && (!item.textColor.isValid()
                                            || item.textColor.lightness() < 180))
                itemBg = Qt::white;
        }

        for (const QRectF &rect : eraseRects) {

            const QRectF clean = rect.adjusted(-0.55, -0.45, 0.55, 0.45);
            const QColor localBg = sampledBackground(original, rect, scale,
                                                     item.textColor, itemBg);
            painter.fillRect(QRectF(clean.topLeft() * scale,
                                    clean.size() * scale), localBg);
        }
    }
    painter.end();
    return erased;
}

#endif

QList<DocxPage> DocumentExporter::allPageContent(const QList<int> &pages) const
{
    QList<DocxPage> result;
#ifdef HAVE_PDF_RENDERING
    if (!m_src.renderer || m_src.pageCount <= 0) return result;
    QList<int> wanted = pages;
    if (wanted.isEmpty())
        for (int p = 0; p < m_src.pageCount; ++p) wanted.append(p);
    result.reserve(wanted.size());
    for (int i : wanted) {
        if (i < 0 || i >= m_src.pageCount) continue;
        DocxPage page;
        page.pageSizePt = m_src.renderer->pageSizePts(i);
        if (m_src.provider)
            page.items = m_src.provider->pageItemsForExport(i);

        constexpr qreal exportScale = 2.0;
        const QImage originalRaster = m_src.renderer->renderPage(i, exportScale);
        page.background = eraseTextRuns(originalRaster, page.items, i, exportScale);
        DocxLayoutInput layout;
        layout.items      = page.items;
        layout.original   = originalRaster;
        layout.erased     = page.background;
        layout.pageSizePt = page.pageSizePt;
        layout.scale      = exportScale;
        page.blocks = buildDocxBlocks(layout, &page.marginsPt);

        result.append(std::move(page));
    }
#endif
    return result;
}

bool DocumentExporter::exportPagesToImages(const QString &outputPath,
                                           int quality,
                                           const QList<int> &pages) const
{
#ifdef HAVE_PDF_RENDERING
    if (!m_src.renderer || m_src.pageCount <= 0 || outputPath.isEmpty()) return false;
    QList<int> wanted = pages;
    if (wanted.isEmpty())
        for (int p = 0; p < m_src.pageCount; ++p) wanted.append(p);
    if (wanted.isEmpty()) return false;

    const QFileInfo out(outputPath);
    const qreal scale = quality >= 95 ? 3.0 : quality >= 80 ? 2.0
                                      : quality >= 55 ? 1.5 : 1.0;
    for (int i : wanted) {
        if (i < 0 || i >= m_src.pageCount) continue;
        QImage image = m_src.renderer->renderPage(i, scale, m_src.session);
        if (image.isNull()) return false;
        const QString path = wanted.size() == 1
            ? outputPath
            : out.dir().filePath(out.completeBaseName()
                                 + QStringLiteral("_page_%1.png").arg(i + 1));
        if (!image.save(path, "PNG", qBound(0, quality, 100))) return false;
    }
    return true;
#else
    Q_UNUSED(outputPath);
    Q_UNUSED(quality);
    Q_UNUSED(pages);
    return false;
#endif
}

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_QT_PRINT)
bool DocumentExporter::printPages(QPrinter *printer, const QList<int> &pages) const
{
    if (!printer || !m_src.renderer || m_src.pageCount <= 0) return false;

    QList<int> wanted;
    for (int p : pages)
        if (p >= 0 && p < m_src.pageCount) wanted.append(p);
    if (pages.isEmpty())
        for (int p = 0; p < m_src.pageCount; ++p) wanted.append(p);
    if (wanted.isEmpty()) return false;

    const qreal dpi   = qBound(72.0, qreal(printer->resolution()), 300.0);
    const qreal scale = dpi / PdfRenderer::kPtsPerInch;

    QPainter painter;
    if (!painter.begin(printer)) return false;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    bool firstPage = true;
    for (int i : wanted) {

        if (!firstPage && !printer->newPage()) {
            painter.end();
            return false;
        }
        firstPage = false;

        QImage image = m_src.renderer->renderPage(i, scale, m_src.session);
        if (image.isNull()) continue;

        const QRectF target = painter.viewport();
        if (target.isEmpty()) continue;

        const QSizeF pageSz(image.size());
        const auto fitScale = [&target](const QSizeF &s) {
            return qMin(target.width() / s.width(), target.height() / s.height());
        };
        const bool rotate = fitScale(pageSz.transposed()) > fitScale(pageSz) * 1.05;

        QSizeF drawn = rotate ? pageSz.transposed() : pageSz;
        drawn.scale(target.size(), Qt::KeepAspectRatio);
        const QRectF dest(target.x() + (target.width()  - drawn.width())  / 2.0,
                          target.y() + (target.height() - drawn.height()) / 2.0,
                          drawn.width(), drawn.height());

        if (rotate) {
            painter.save();
            painter.translate(dest.center());
            painter.rotate(90);

            painter.drawImage(QRectF(-dest.height() / 2.0, -dest.width() / 2.0,
                                     dest.height(), dest.width()), image);
            painter.restore();
        } else {
            painter.drawImage(dest, image);
        }
    }

    painter.end();
    return true;
}
#endif
