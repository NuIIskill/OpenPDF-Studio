#include "engine/edit/DocumentExporter.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/edit/ContentMap.hpp"
#  include "engine/edit/ContentModel.hpp"
#  include "engine/edit/DocxLayout.hpp"
#  include "engine/edit/EditSession.hpp"
#  include "engine/ocr/OcrEngine.hpp"
#  include "engine/render/PdfRenderer.hpp"
#  ifdef HAVE_QT_PDF
#    include "engine/edit/PdfTextExtractor.hpp"
#    include <QPdfDocument>
#    include <QPdfSelection>
#    include <QRegularExpression>
#  endif
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

// Style donor for a decoded text rect: a detected item that actually covers
// it. The previous nearest-centre match had no distance limit and no overlap
// test, so a table header cell several lines away could hand a body paragraph
// its brown fill colour and its font size.
const ContentItem *styleDonor(const QList<ContentItem> &detected,
                              const QRectF &rect)
{
    const ContentItem *best = nullptr;
    double bestScore = 0.0;
    for (const ContentItem &candidate : detected) {
        if (!candidate.isTextual()) continue;
        const QRectF hit = candidate.bounds.intersected(rect);
        if (hit.isEmpty()) continue;
        // Vertical agreement weighs more than horizontal: one detected line
        // may be split across several decoded runs and vice versa, but a run
        // and its donor always share the same baseline band.
        const double vShare = hit.height() / qMax(1e-6, rect.height());
        const double hShare = hit.width()  / qMax(1e-6, rect.width());
        const double score  = vShare * 0.65 + hShare * 0.35;
        if (score > bestScore) {
            bestScore = score;
            best = &candidate;
        }
    }
    // Must be genuinely covered, not merely adjacent.
    return bestScore >= 0.5 ? best : nullptr;
}

// Qt reports one selection polygon per visual line for most PDFs, but one per
// GLYPH for others (Writer output among them). A single glyph cannot be
// selected reliably — getSelection runs from the character under the start
// point to the one under the end point, and for a 6 pt box the end point
// already sits in the next glyph's cell, so every query returns two characters
// and the line arrives as "DDiieesseer AAbbssaattz". Merging the glyph boxes
// back into the line runs the rest of the pipeline expects avoids the problem
// instead of trying to out-guess it.
QList<QRectF> mergeGlyphBoxes(QList<QRectF> boxes)
{
    std::sort(boxes.begin(), boxes.end(), [](const QRectF &a, const QRectF &b) {
        if (std::abs(a.center().y() - b.center().y()) > 1.5)
            return a.center().y() < b.center().y();
        return a.left() < b.left();
    });

    // Merge only up to a typical glyph advance, which yields word-sized runs.
    // Merging whole lines instead swallowed the gap between two narrow table
    // columns, and the classifier — which splits lines into cells itself — no
    // longer had anything to split.
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

} // namespace
#endif

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_QT_PDF)

QList<ContentItem> DocumentExporter::decodedTextItems(
    int page, const QSizeF &pageSizePt, const QList<ContentItem> &detected) const
{
    const QPdfSelection all = m_src.document->getAllText(page);
    QList<ContentCluster> clusters;
    if (all.isValid()) {
        QList<QRectF> boxes;
        for (const QPolygonF &polygon : all.bounds()) {
            const QRectF rect = polygon.boundingRect();
            if (!rect.isEmpty()) boxes.append(rect);
        }
        // Per-glyph polygons are merged into line runs first — see
        // mergeGlyphBoxes. Judged on the median box, so a page that
        // happens to contain one narrow line is not misread.
        bool fromGlyphs = false;
        {
            QList<double> widths, heights;
            for (const QRectF &r : boxes) {
                widths.append(r.width());
                heights.append(r.height());
            }
            std::sort(widths.begin(), widths.end());
            std::sort(heights.begin(), heights.end());
            if (!boxes.isEmpty()
                    && widths[widths.size() / 2]
                           < heights[heights.size() / 2] * 1.6) {
                boxes = mergeGlyphBoxes(std::move(boxes));
                fromGlyphs = true;
            }
        }

        for (const QRectF &rect : boxes) {
            // A line box hugs its glyphs so tightly that querying it
            // verbatim drops the last character ("ca. 200 kcal" came
            // back as "ca. 200 kca"); a point of slack recovers it.
            // Word-sized runs merged out of glyph boxes must NOT be
            // widened — the slack reaches into the following run and
            // repeats its first character ("mit fett" → "mitfettt").
            const QRectF query = fromGlyphs
                ? rect : rect.adjusted(-0.5, 0.0, 1.0, 0.0);
            const QPdfSelection selection = m_src.document->getSelection(
                page, query.topLeft(), query.bottomRight());
            QString text = selection.text();
            // Qt may return the queried visual line plus a fragment of
            // the following line when selection polygons touch. One
            // polygon represents exactly one visual line here, so keep
            // only that first line and discard PDF non-characters.
            // Qt uses U+FFFE at a line-end discretionary hyphen in
            // several Writer-generated PDFs. It is visually a '-' and
            // must not silently join words such as "Hardware-Lifecycle".
            text.replace(QChar(0xFFFE), QStringLiteral("-"));
            text.replace(QChar(0xFFFF), QString{});
            text = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                              Qt::SkipEmptyParts).value(0).trimmed();
            // A narrow single glyph — a lone "C" in a table cell — often
            // returns nothing for its exact polygon. Retry once with a
            // slightly wider query before assuming anything about it;
            // isolated cells have no neighbour close enough to bleed in.
            if (text.isEmpty()) {
                const QRectF wider = rect.adjusted(-1.2, -0.8, 1.2, 0.8);
                text = m_src.document->getSelection(page, wider.topLeft(),
                                                    wider.bottomRight()).text();
                text.replace(QChar(0xFFFE), QStringLiteral("-"));
                text.replace(QChar(0xFFFF), QString{});
                text = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                  Qt::SkipEmptyParts).value(0).trimmed();
            }
            // OpenSymbol bullet polygons intentionally have no Unicode
            // selection text in Qt although the page-wide text stream
            // contains U+2022. Their tiny square geometry is unambiguous
            // — but only once the retry above has come back empty too,
            // or real letters get turned into bullets and the classifier
            // then folds their whole table row into one bullet line.
            if (text.isEmpty() && rect.width() <= 6.0 && rect.height() <= 6.0)
                text = QStringLiteral("•");
            if (text.isEmpty()) continue;

            ContentCluster cluster;
            cluster.bounds = rect;
            cluster.text = text;
            cluster.exactWidth = true;

            if (const ContentItem *style = styleDonor(detected, rect)) {
                cluster.rawFontName = style->rawFontName;
                cluster.textColor = style->textColor;
                cluster.fontSizePt = style->fontSizePt;   // 0 = unknown
            }
            clusters.append(std::move(cluster));
        }
    }

    // Font size comes from the PDF's own /Tf operand. Deriving it from
    // the height of Qt's selection polygon measures the ink of whatever
    // glyphs the line happens to contain: "Geschmack" (no descender)
    // and "nussig, leicht bitter, cremig" sit in the same 8 pt table
    // row, yet their polygons differ by a third — which is exactly how
    // one table ended up rendered in three different sizes. Lines with
    // no donor fall back to geometry, scaled by the median size/height
    // ratio actually measured on this page rather than a fixed guess.
    {
        QList<double> ratios;
        for (const ContentCluster &c : clusters)
            if (c.fontSizePt > 0.0 && c.bounds.height() > 0.5)
                ratios.append(c.fontSizePt / c.bounds.height());
        double ratio = 0.95;
        if (!ratios.isEmpty()) {
            std::sort(ratios.begin(), ratios.end());
            ratio = ratios[ratios.size() / 2];
        }
        for (ContentCluster &c : clusters)
            if (c.fontSizePt <= 0.0)
                c.fontSizePt = qMax(2.0, c.bounds.height() * ratio);
    }
    // Keep one export item per visual PDF line/cell. Vertical merging
    // is useful for the editor, but Word's line spacing would move
    // merged table rows away from the original raster grid.
    QList<ContentItem> decoded = classifyContentClusters(
        std::move(clusters), false);
    if (!decoded.isEmpty()) {
        // The classifier resolves font style from rawFontName. Copy the
        // covering detected fill explicitly because it is page-paint
        // metadata rather than a property of Qt's text polygons.
        // ContentMap already assigns bgColor by containment (fill rect
        // shrunk 2 pt so borders never match); matching by centre
        // distance here threw that away and let a table header donate
        // its brown to a body line, which then got erased in brown.
        for (ContentItem &item : decoded) {
            if (const ContentItem *style = styleDonor(detected, item.bounds))
                item.bgColor = style->bgColor;
        }

        // Some PDFs have no usable ToUnicode map: Qt renders the glyphs
        // correctly but extraction returns Greek/symbol characters for
        // German prose. Detect that signature and OCR only those lines.
        bool needsOcr = false;
        for (const ContentItem &item : decoded) {
            int greek = 0, letters = 0;
            for (const QChar c : item.text) {
                if (!c.isLetter()) continue;
                ++letters;
                const ushort u = c.unicode();
                if ((u >= 0x0370 && u <= 0x03FF)
                        || (u >= 0x1F00 && u <= 0x1FFF))
                    ++greek;
            }
            if (letters >= 4 && greek * 4 >= letters) {
                needsOcr = true;
                break;
            }
        }
        if (needsOcr && m_src.ocr && m_src.ocr->isReady()) {
            constexpr qreal ocrScale = 2.5;
            const QImage ocrImage = m_src.renderer->renderPage(page, ocrScale);
            const QList<OcrEngine::Block> blocks = m_src.ocr->recognizePage(
                ocrImage, pageSizePt, ocrScale);
            if (!blocks.isEmpty()) {
                QList<ContentItem> ocrItems;
                ocrItems.reserve(blocks.size());
                for (const OcrEngine::Block &block : blocks) {
                    if (!block.isValid()) continue;
                    ContentItem item;
                    item.type = ContentItem::Type::Text;
                    item.bounds = block.pdfBounds;
                    item.text = block.text;
                    item.fontSizePt = qMax(2.0, block.pdfBounds.height() * 0.72);

                    const ContentItem *style = styleDonor(detected,
                                                          item.bounds);
                    if (style) {
                        if (style->fontSizePt > 0.0)
                            item.fontSizePt = style->fontSizePt;
                        item.fontFamily = style->fontFamily;
                        item.rawFontName = style->rawFontName;
                        item.bold = style->bold;
                        item.italic = style->italic;
                        item.textColor = style->textColor;
                        item.bgColor = style->bgColor;
                    }
                    ocrItems.append(std::move(item));
                }
                if (!ocrItems.isEmpty()) decoded = std::move(ocrItems);
            }
        }
        for (const ContentItem &item : detected)
            if (!item.isTextual()) decoded.append(item);
        return decoded;
    }
    // Nothing decodable on this page — keep what the scanner found.
    return detected;
}

QList<ContentItem> DocumentExporter::wholePageFallback(
    int page, const QSizeF &pageSizePt) const
{
    ContentItem item;
    item.type   = ContentItem::Type::Paragraph;
    item.bounds = QRectF(54.0, 54.0,
                         qMax(1.0, pageSizePt.width()  - 108.0),
                         qMax(1.0, pageSizePt.height() - 108.0));
    item.text       = m_src.document->getAllText(page).text();
    item.fontSizePt = 11.0;
    if (item.text.trimmed().isEmpty()) return {};
    return { item };
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
#  ifdef HAVE_QT_PDF
        if (m_src.extractor)
            eraseRects = m_src.extractor->glyphRects(page, item.bounds, {});
#  endif
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
            // A dark probe is almost certainly the preceding glyph or
            // a cell rule, not the page/cell background.
            if (itemBg.lightness() < 45 && (!item.textColor.isValid()
                                            || item.textColor.lightness() < 180))
                itemBg = Qt::white;
        }

        // Join exact word/glyph boxes per visual line. ContentItem width
        // is estimated by the qpdf scanner and can extend far past the
        // last word; filling that estimate creates the visible colour
        // bars. Line unions retain the stronger duplicate-text cleanup
        // while stopping at the actual rendered line end.
        QList<QRectF> lineRects;
        for (const QRectF &rect : eraseRects) {
            int host = -1;
            for (int line = 0; line < lineRects.size(); ++line) {
                const double tolerance = qMax(lineRects[line].height(),
                                              rect.height()) * 0.60;
                if (qAbs(lineRects[line].center().y() - rect.center().y())
                        <= tolerance) {
                    host = line;
                    break;
                }
            }
            if (host >= 0)
                lineRects[host] = lineRects[host].united(rect);
            else
                lineRects.append(rect);
        }
        for (const QRectF &line : lineRects) {
            const QRectF cleanLine = line.adjusted(-0.35, -0.25, 0.75, 0.25);
            painter.fillRect(QRectF(cleanLine.topLeft() * scale,
                                    cleanLine.size() * scale), itemBg);
        }

        for (const QRectF &rect : eraseRects) {
            // Qt's polygons hug the visible glyphs. Half a point covers
            // antialiasing fringes without crossing table borders.
            const QRectF clean = rect.adjusted(-0.55, -0.45, 0.55, 0.45);
            painter.fillRect(QRectF(clean.topLeft() * scale,
                                    clean.size() * scale), itemBg);
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
            page.items = m_src.provider->pageItems(i);
#  ifdef HAVE_QT_PDF
        // qpdf exposes raw string bytes. For embedded fonts with a custom
        // encoding those bytes are character codes, not Unicode. Build the
        // complete text model from Qt's decoded polygons instead; qpdf items
        // remain useful only as nearby style/colour metadata sources.
        if (m_src.document)
            page.items = decodedTextItems(i, page.pageSizePt, page.items);
        if (page.items.isEmpty() && m_src.document)
            page.items = wholePageFallback(i, page.pageSizePt);
#  endif
        // Preserve images/vector graphics as a raster layer, then remove the
        // native PDF glyphs at their exact renderer-reported rectangles. DOCX
        // text boxes are placed over this cleaned layer and remain editable.
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
        QImage image = m_src.renderer->renderPage(i, scale);
        if (image.isNull()) return false;
        if (m_src.session) m_src.session->applyToImage(i, image, scale);
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

    // Rasterising at the printer's own resolution is what makes the print
    // sharp, but 600 dpi on A4 is a ~140 MB image per page and nothing of it
    // is visible on paper. Cap at 300 dpi and let QPainter do the last step.
    const qreal dpi   = qBound(72.0, qreal(printer->resolution()), 300.0);
    const qreal scale = dpi / PdfRenderer::kPtsPerInch;

    QPainter painter;
    if (!painter.begin(printer)) return false;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    bool firstPage = true;
    for (int i : wanted) {
        // newPage() goes *between* sheets — calling it up front ejects a blank
        // one. A page that fails to render still gets its sheet so that what
        // comes out matches the page numbers the user asked for.
        if (!firstPage && !printer->newPage()) {
            painter.end();
            return false;
        }
        firstPage = false;

        QImage image = m_src.renderer->renderPage(i, scale);
        if (image.isNull()) continue;
        // Same source of truth as the image export: the session holds edits
        // that are not in the rendered file yet, and printing must show them.
        if (m_src.session) m_src.session->applyToImage(i, image, scale);

        const QRectF target = painter.viewport();
        if (target.isEmpty()) continue;

        // Auto-rotate like every other PDF printer does: a landscape page on a
        // portrait sheet otherwise prints at ~70 % with two empty bands, and
        // orientation is per document in the print dialog while a PDF may mix
        // both. Only rotate when it genuinely buys size.
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
            // Under a 90° rotation the local rect's width becomes the drawn
            // height and vice versa, so it lands exactly on dest.
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
