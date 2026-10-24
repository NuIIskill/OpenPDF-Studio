#include "engine/export/DocumentExporter.hpp"
#include "engine/document/PdfBackend.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/edit/ContentMap.hpp"
#  include "engine/edit/ContentModel.hpp"
#  include "engine/export/DocxLayout.hpp"
#  include "engine/export/DocxXml.hpp"
#  include "engine/edit/EditSession.hpp"
#  include "engine/ocr/OcrEngine.hpp"
#  include "engine/render/PdfRenderer.hpp"
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

QList<DocxPage> DocumentExporter::allPageContent(const QList<int> &pages,
                                                 const DocxExportOptions &options,
                                                 const std::function<bool(int, int)> &progress) const
{
    QList<DocxPage> result;
#ifdef HAVE_PDF_RENDERING
    if (!m_src.renderer || m_src.pageCount <= 0) return result;
    QList<int> wanted = pages;
    if (wanted.isEmpty())
        for (int p = 0; p < m_src.pageCount; ++p) wanted.append(p);
    result.reserve(wanted.size());
    for (int n = 0; n < wanted.size(); ++n) {
        if (progress && !progress(n, wanted.size())) return {};
        const int i = wanted[n];
        if (i < 0 || i >= m_src.pageCount) continue;
        DocxPage page;
        page.pageSizePt = m_src.renderer->pageSizePts(i);
        if (m_src.provider)
            page.items = m_src.provider->pageItemsForExport(i);

        constexpr qreal exportScale = 2.0;
        const QImage originalRaster = m_src.renderer->renderPage(i, exportScale);
        DocxLayoutInput layout;
        layout.items      = page.items;
        layout.original   = originalRaster;
        layout.erased     = eraseTextRuns(originalRaster, page.items, i, exportScale);
        layout.pageSizePt = page.pageSizePt;
        layout.scale      = exportScale;
        layout.options    = options;
        page.blocks = buildDocxBlocks(layout, &page.marginsPt);
        if (page.blocks.isEmpty())
            page.background = DocxXml::encodePicture(layout.erased, options);

        result.append(std::move(page));
    }
#else
    Q_UNUSED(pages) Q_UNUSED(options) Q_UNUSED(progress)
#endif
    return result;
}

bool DocumentExporter::exportPagesToImages(const QString &outputPath,
                                           int quality,
                                           const QList<int> &pages,
                                           const std::function<bool(int, int)> &progress) const
{
#ifdef HAVE_PDF_RENDERING
    if (!m_src.renderer || m_src.pageCount <= 0 || outputPath.isEmpty()) return false;
    QList<int> wanted = pages;
    if (wanted.isEmpty())
        for (int p = 0; p < m_src.pageCount; ++p) wanted.append(p);
    if (wanted.isEmpty()) return false;

    const QFileInfo out(outputPath);
    const QString suffix = out.suffix().toLower();
    const bool jpeg = suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg");
    const qreal scale = quality >= 95 ? 3.0 : quality >= 80 ? 2.0
                                      : quality >= 55 ? 1.5 : 1.0;
    for (int n = 0; n < wanted.size(); ++n) {
        if (progress && !progress(n, wanted.size())) return false;
        const int i = wanted[n];
        if (i < 0 || i >= m_src.pageCount) continue;
        QImage image = m_src.renderer->renderPage(i, scale, m_src.session);
        if (image.isNull()) return false;
        const QString path = wanted.size() == 1
            ? outputPath
            : out.dir().filePath(out.completeBaseName()
                                 + QStringLiteral("_page_%1.").arg(i + 1)
                                 + (jpeg ? suffix : QStringLiteral("png")));
        if (jpeg && image.hasAlphaChannel()) {
            QImage opaque(image.size(), QImage::Format_RGB32);
            opaque.fill(Qt::white);
            QPainter painter(&opaque);
            painter.drawImage(0, 0, image);
            painter.end();
            image = opaque;
        }
        // PNG is lossless, so the quality only decides the resolution above.
        const bool saved = jpeg ? image.save(path, "JPEG", qBound(0, quality, 100))
                                : image.save(path, "PNG");
        if (!saved) return false;
    }
    return true;
#else
    Q_UNUSED(outputPath);
    Q_UNUSED(quality);
    Q_UNUSED(pages);
    Q_UNUSED(progress);
    return false;
#endif
}
