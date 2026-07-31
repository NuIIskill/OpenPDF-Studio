#include "DocumentExporter.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "ContentMap.hpp"
#  include "ContentModel.hpp"
#  include "EditSession.hpp"
#  include "engine/ocr/OcrEngine.hpp"
#  include "engine/view/PdfRenderer.hpp"
#  ifdef HAVE_QT_PDF
#    include "PdfTextExtractor.hpp"
#    include <QPdfDocument>
#    include <QPdfSelection>
#    include <QRegularExpression>
#  endif
#endif

#include <QDir>
#include <QFileInfo>
#include <QLineF>
#include <QPainter>

QList<DocxPage> DocumentExporter::allPageContent() const
{
    QList<DocxPage> result;
#ifdef HAVE_PDF_RENDERING
    if (!m_src.renderer || m_src.pageCount <= 0) return result;
    result.reserve(m_src.pageCount);
    for (int i = 0; i < m_src.pageCount; ++i) {
        DocxPage page;
        page.pageSizePt = m_src.renderer->pageSizePts(i);
        if (m_src.provider)
            page.items = m_src.provider->pageItems(i);
#  ifdef HAVE_QT_PDF
        // qpdf exposes raw string bytes. For embedded fonts with a custom
        // encoding those bytes are character codes, not Unicode. Build the
        // complete text model from Qt's decoded polygons instead; qpdf items
        // remain useful only as nearby style/colour metadata sources.
        if (m_src.document) {
            const QList<ContentItem> detected = page.items;
            const QPdfSelection all = m_src.document->getAllText(i);
            QList<ContentCluster> clusters;
            if (all.isValid()) {
                for (const QPolygonF &polygon : all.bounds()) {
                    const QRectF rect = polygon.boundingRect();
                    if (rect.isEmpty()) continue;
                    const QRectF query = rect;
                    const QPdfSelection selection = m_src.document->getSelection(
                        i, query.topLeft(), query.bottomRight());
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
                    // OpenSymbol bullet polygons intentionally have no Unicode
                    // selection text in Qt although the page-wide text stream
                    // contains U+2022. Their tiny square geometry is unambiguous.
                    if (text.isEmpty() && rect.width() <= 6.0 && rect.height() <= 6.0)
                        text = QStringLiteral("•");
                    if (text.isEmpty()) continue;

                    ContentCluster cluster;
                    cluster.bounds = rect;
                    cluster.text = text;
                    cluster.fontSizePt = qMax(2.0, rect.height() * 0.74);
                    cluster.exactWidth = true;

                    const ContentItem *style = nullptr;
                    double bestDistance = 1e18;
                    for (const ContentItem &candidate : detected) {
                        if (!candidate.isTextual()) continue;
                        const double distance = QLineF(candidate.bounds.center(),
                                                       rect.center()).length();
                        if (distance < bestDistance) {
                            bestDistance = distance;
                            style = &candidate;
                        }
                    }
                    if (style) {
                        cluster.rawFontName = style->rawFontName;
                        cluster.textColor = style->textColor;
                        if (style->fontSizePt > 0.0)
                            cluster.fontSizePt = qMin(style->fontSizePt,
                                                      rect.height() * 0.82);
                    }
                    clusters.append(std::move(cluster));
                }
            }
            // Keep one export item per visual PDF line/cell. Vertical merging
            // is useful for the editor, but Word's line spacing would move
            // merged table rows away from the original raster grid.
            QList<ContentItem> decoded = classifyContentClusters(
                std::move(clusters), false);
            if (!decoded.isEmpty()) {
                // The classifier resolves font style from rawFontName. Copy the
                // nearest detected fill explicitly because it is page-paint
                // metadata rather than a property of Qt's text polygons.
                for (ContentItem &item : decoded) {
                    const ContentItem *style = nullptr;
                    double bestDistance = 1e18;
                    for (const ContentItem &candidate : detected) {
                        if (!candidate.isTextual()) continue;
                        const double distance = QLineF(candidate.bounds.center(),
                                                       item.bounds.center()).length();
                        if (distance < bestDistance) {
                            bestDistance = distance;
                            style = &candidate;
                        }
                    }
                    if (style) item.bgColor = style->bgColor;
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
                    const QImage ocrImage = m_src.renderer->renderPage(i, ocrScale);
                    const QList<OcrEngine::Block> blocks = m_src.ocr->recognizePage(
                        ocrImage, page.pageSizePt, ocrScale);
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

                            const ContentItem *style = nullptr;
                            double bestDistance = 1e18;
                            for (const ContentItem &candidate : detected) {
                                if (!candidate.isTextual()) continue;
                                const double distance = QLineF(candidate.bounds.center(),
                                                               item.bounds.center()).length();
                                if (distance < bestDistance) {
                                    bestDistance = distance;
                                    style = &candidate;
                                }
                            }
                            if (style) {
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
                page.items = std::move(decoded);
            }
        }
        if (page.items.isEmpty() && m_src.document) {
            ContentItem item;
            item.type = ContentItem::Type::Paragraph;
            item.bounds = QRectF(54.0, 54.0,
                                 qMax(1.0, page.pageSizePt.width() - 108.0),
                                 qMax(1.0, page.pageSizePt.height() - 108.0));
            item.text = m_src.document->getAllText(i).text();
            item.fontSizePt = 11.0;
            if (!item.text.trimmed().isEmpty()) page.items.append(item);
        }
#  endif
        // Preserve images/vector graphics as a raster layer, then remove the
        // native PDF glyphs at their exact renderer-reported rectangles. DOCX
        // text boxes are placed over this cleaned layer and remain editable.
        constexpr qreal exportScale = 2.0;
        page.background = m_src.renderer->renderPage(i, exportScale);
        if (!page.background.isNull()) {
            QPainter painter(&page.background);
            painter.setRenderHint(QPainter::Antialiasing, false);
            if (m_src.session) {
                for (const EditSession::ImageEdit &edit : m_src.session->imageEdits()) {
                    if (edit.page != i || edit.image.isNull()) continue;
                    painter.drawImage(QRectF(edit.pdfBounds.topLeft() * exportScale,
                                             edit.pdfBounds.size() * exportScale),
                                      edit.image);
                }
            }
            for (const ContentItem &item : page.items) {
                if (!item.isTextual() || item.text.trimmed().isEmpty()) continue;
                QList<QRectF> eraseRects;
#  ifdef HAVE_QT_PDF
                if (m_src.extractor)
                    eraseRects = m_src.extractor->glyphRects(i, item.bounds, {});
#  endif
                if (eraseRects.isEmpty()) eraseRects.append(item.bounds);

                QColor itemBg = item.bgColor;
                if (!itemBg.isValid()) {
                    const QPointF probePt((item.bounds.left() - 2.0) * exportScale,
                                          item.bounds.center().y() * exportScale);
                    const int x = qBound(0, qRound(probePt.x()),
                                         page.background.width() - 1);
                    const int y = qBound(0, qRound(probePt.y()),
                                         page.background.height() - 1);
                    itemBg = page.background.pixelColor(x, y);
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
                    painter.fillRect(QRectF(cleanLine.topLeft() * exportScale,
                                            cleanLine.size() * exportScale), itemBg);
                }

                for (const QRectF &rect : eraseRects) {
                    // Qt's polygons hug the visible glyphs. Half a point covers
                    // antialiasing fringes without crossing table borders.
                    const QRectF clean = rect.adjusted(-0.55, -0.45, 0.55, 0.45);
                    painter.fillRect(QRectF(clean.topLeft() * exportScale,
                                            clean.size() * exportScale), itemBg);
                }
            }
        }
        result.append(std::move(page));
    }
#endif
    return result;
}

bool DocumentExporter::exportPagesToImages(const QString &outputPath,
                                           int quality) const
{
#ifdef HAVE_PDF_RENDERING
    if (!m_src.renderer || m_src.pageCount <= 0 || outputPath.isEmpty()) return false;
    const QFileInfo out(outputPath);
    const qreal scale = quality >= 95 ? 3.0 : quality >= 80 ? 2.0
                                      : quality >= 55 ? 1.5 : 1.0;
    for (int i = 0; i < m_src.pageCount; ++i) {
        QImage image = m_src.renderer->renderPage(i, scale);
        if (image.isNull()) return false;
        if (m_src.session) m_src.session->applyToImage(i, image, scale);
        const QString path = m_src.pageCount == 1
            ? outputPath
            : out.dir().filePath(out.completeBaseName()
                                 + QStringLiteral("_page_%1.png").arg(i + 1));
        if (!image.save(path, "PNG", qBound(0, quality, 100))) return false;
    }
    return true;
#else
    Q_UNUSED(outputPath);
    Q_UNUSED(quality);
    return false;
#endif
}
