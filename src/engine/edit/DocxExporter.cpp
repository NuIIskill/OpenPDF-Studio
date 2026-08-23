#include "engine/edit/DocxExporter.hpp"
#include "engine/edit/DocxXml.hpp"
#include "engine/edit/ZipWriter.hpp"

#include <QByteArray>
#include <QBuffer>
#include <QPainter>
#include <QFile>

#include <algorithm>
#include <cmath>
#include <utility>

// The fragments below are assembled from these.
using namespace DocxXml;

static QByteArray buildDocument(const QList<QString> &pageTexts, const QString &title)
{
    QString x;
    x += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
    x += QStringLiteral("<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
    x += QStringLiteral("<w:body>");

    if (!title.isEmpty()) {
        x += QStringLiteral("<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr>"
                            "<w:r><w:t>");
        x += xmlEsc(title);
        x += QStringLiteral("</w:t></w:r></w:p>");
    }

    for (int pg = 0; pg < pageTexts.size(); ++pg) {
        if (pg > 0)
            x += QStringLiteral("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>");

        // Group consecutive non-blank lines into paragraphs;
        // blank lines act as paragraph separators (like in a word processor).
        QString para;
        const auto emit = [&]() {
            if (para.isEmpty()) return;
            x += QStringLiteral("<w:p><w:r><w:t xml:space=\"preserve\">");
            x += xmlEsc(para);
            x += QStringLiteral("</w:t></w:r></w:p>");
            para.clear();
        };

        for (const QString &line : pageTexts[pg].split(QLatin1Char('\n'))) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) {
                emit();                          // flush accumulated paragraph
            } else {
                if (!para.isEmpty()) para += QLatin1Char(' ');
                para += trimmed;
            }
        }
        emit(); // flush last paragraph
    }

    x += QStringLiteral("<w:sectPr/></w:body></w:document>");
    return x.toUtf8();
}

static bool prefersSemanticLayout(const QList<DocxPage> &pages)
{
    int textual = 0;
    int coloured = 0;
    for (const DocxPage &page : pages) {
        for (const ContentItem &item : page.items) {
            if (item.type == ContentItem::Type::Image
                    || item.type == ContentItem::Type::Media)
                return false;
            if (!item.isTextual()) continue;
            ++textual;
            // Coloured PDF fills usually indicate a designed/graphic document
            // whose exact appearance is better served by positioned layout.
            if (item.bgColor.isValid() && item.bgColor != QColor(Qt::white)
                    && item.bgColor.lightness() < 245)
                ++coloured;
        }
    }
    return textual >= 4 && coloured * 5 < textual;
}

static QByteArray buildSemanticDocument(const QList<DocxPage> &pages)
{
    QString x = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:body>");

    constexpr double horizontalMarginPt = 68.0;
    constexpr double verticalMarginPt = 36.0;
    for (int pg = 0; pg < pages.size(); ++pg) {
        if (pg > 0)
            x += QStringLiteral("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>");

        QList<ContentItem> items;
        for (const ContentItem &item : pages[pg].items)
            if (item.isTextual() && !item.text.trimmed().isEmpty()) items.append(item);
        std::sort(items.begin(), items.end(), [](const ContentItem &a, const ContentItem &b) {
            if (std::abs(a.bounds.top() - b.bounds.top()) > 1.5)
                return a.bounds.top() < b.bounds.top();
            return a.bounds.left() < b.bounds.left();
        });

        double previousBottom = verticalMarginPt;
        for (int i = 0; i < items.size();) {
            const double rowY = items[i].bounds.center().y();
            QList<ContentItem> row;
            int j = i;
            while (j < items.size()) {
                const double tolerance = qMax(items[i].bounds.height(),
                                              items[j].bounds.height()) * 0.45;
                if (qAbs(items[j].bounds.center().y() - rowY) > tolerance) break;
                row.append(items[j++]);
            }
            std::sort(row.begin(), row.end(), [](const ContentItem &a, const ContentItem &b) {
                return a.bounds.left() < b.bounds.left();
            });

            const double rowTop = row.first().bounds.top();
            // Word adds its own line/table height. Applying the full PDF gap on
            // top double-counts vertical space and pushes signatures/footer to
            // an extra page. 75% preserves section spacing without overflow.
            const int before = qRound(qMax(0.0, rowTop - previousBottom) * 15.0);
            const bool tableRow = row.size() > 1
                && std::any_of(row.cbegin(), row.cend(), [](const ContentItem &item) {
                    return item.type == ContentItem::Type::TableCell;
                });

            if (tableRow) {
                x += QStringLiteral("<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/>"
                                    "<w:tblBorders><w:top w:val=\"single\" w:sz=\"2\" w:color=\"D1D5DB\"/>"
                                    "<w:left w:val=\"single\" w:sz=\"2\" w:color=\"D1D5DB\"/>"
                                    "<w:bottom w:val=\"single\" w:sz=\"2\" w:color=\"D1D5DB\"/>"
                                    "<w:right w:val=\"single\" w:sz=\"2\" w:color=\"D1D5DB\"/>"
                                    "<w:insideH w:val=\"single\" w:sz=\"2\" w:color=\"D1D5DB\"/>"
                                    "<w:insideV w:val=\"single\" w:sz=\"2\" w:color=\"D1D5DB\"/>"
                                    "</w:tblBorders></w:tblPr><w:tr>");
                for (int col = 0; col < row.size(); ++col) {
                    const double nextX = col + 1 < row.size()
                        ? row[col + 1].bounds.left()
                        : pages[pg].pageSizePt.width() - horizontalMarginPt;
                    const int width = qMax(240, qRound((nextX - row[col].bounds.left()) * 20.0));
                    x += QStringLiteral("<w:tc><w:tcPr><w:tcW w:w=\"%1\" w:type=\"dxa\"/>"
                                        "<w:tcMar><w:top w:w=\"20\" w:type=\"dxa\"/>"
                                        "<w:left w:w=\"40\" w:type=\"dxa\"/>"
                                        "<w:bottom w:w=\"20\" w:type=\"dxa\"/>"
                                        "<w:right w:w=\"40\" w:type=\"dxa\"/></w:tcMar>"
                                        "</w:tcPr><w:p><w:pPr><w:spacing w:before=\"0\" w:after=\"0\"/>"
                                        "</w:pPr>").arg(width)
                         + semanticTextRuns(row[col]) + QStringLiteral("</w:p></w:tc>");
                }
                x += QStringLiteral("</w:tr></w:tbl>");
            } else {
                for (int n = 0; n < row.size(); ++n) {
                    const int indent = qRound(qMax(0.0, row[n].bounds.left()
                                                       - horizontalMarginPt) * 20.0);
                    x += semanticParagraph(row[n], n == 0 ? before : 0, indent);
                }
            }
            for (const ContentItem &item : row)
                previousBottom = qMax(previousBottom, item.bounds.bottom());
            i = j;
        }
    }

    const QSizeF size = pages.isEmpty() || pages.first().pageSizePt.isEmpty()
                            ? QSizeF(595.0, 842.0) : pages.first().pageSizePt;
    x += QStringLiteral("<w:sectPr><w:pgSz w:w=\"")
         + QString::number(qRound(size.width() * 20.0))
         + QStringLiteral("\" w:h=\"") + QString::number(qRound(size.height() * 20.0))
         + QStringLiteral("\"/><w:pgMar w:top=\"720\" w:right=\"1360\" w:bottom=\"720\" "
                          "w:left=\"1360\" w:header=\"360\" w:footer=\"360\" w:gutter=\"0\"/>"
                          "</w:sectPr></w:body></w:document>");
    return x.toUtf8();
}

// ── structured export ─────────────────────────────────────────────────────────
// Real WordprocessingML: flowing paragraphs, real tables with cell shading, and
// pictures only for content a word processor cannot express. This is what makes
// the result editable rather than a picture of a document.

// Joins the lines of one paragraph back into flowing text. A hyphen at a line
// end followed by a lower-case letter is a hyphenation break and disappears;
// before an upper-case letter it is a real compound hyphen ("Hardware-
// Lifecycle") and must survive.
static QByteArray buildStructuredDocument(const QList<DocxPage> &pages,
                                          QList<MediaPart> *media,
                                          const DocxExportOptions &opt)
{
    QString x = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document "
        "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\" "
        "xmlns:v=\"urn:schemas-microsoft-com:vml\" "
        "xmlns:o=\"urn:schemas-microsoft-com:office:office\">"
        "<w:body>");

    // One section, so one set of margins for the whole document — but each page
    // measured its own. Taking the narrowest keeps every page's content inside
    // the text area; the difference is handed back to that page's blocks as
    // extra indent, so nothing moves. Using page 1's margins verbatim shifted
    // every later page's tables left by the difference.
    QMarginsF section = pages.isEmpty() ? QMarginsF(56, 45, 56, 45)
                                        : pages.first().marginsPt;
    for (const DocxPage &page : pages) {
        section.setLeft  (qMin(section.left(),   page.marginsPt.left()));
        section.setTop   (qMin(section.top(),    page.marginsPt.top()));
        section.setRight (qMin(section.right(),  page.marginsPt.right()));
        section.setBottom(qMin(section.bottom(), page.marginsPt.bottom()));
    }

    int mediaId = 1;
    int drawingId = 1;
    for (int pg = 0; pg < pages.size(); ++pg) {
        const DocxPage &page = pages[pg];
        const double indentShift = page.marginsPt.left() - section.left();

        // Every picture of this page is anchored from one leading paragraph.
        // Its own height must be negligible or it would push the flow down.
        QString anchors;
        // A mixed document must not flatten every good page merely because one
        // page is a scan. Only that structureless page gets a raster fallback.
        if (page.blocks.isEmpty() && !page.background.isNull()) {
            DocxBlock scan;
            scan.kind    = DocxBlock::Kind::Picture;
            scan.bounds  = QRectF(QPointF(0.0, 0.0), page.pageSizePt);
            scan.picture = page.background;
            QByteArray image;
            const QString ext = encodePicture(scan.picture, opt, &image);
            if (!ext.isEmpty()) {
                MediaPart part;
                part.name  = QStringLiteral("scan%1.%2").arg(pg + 1).arg(ext);
                part.relId = QStringLiteral("rIdImg%1").arg(mediaId);
                part.png   = std::move(image);
                anchors += pictureXml(scan, part.relId, drawingId++);
                media->append(std::move(part));
                ++mediaId;
            }
        }
        for (const DocxBlock &block : page.blocks) {
            if (block.kind == DocxBlock::Kind::Shape) {
                anchors += shapeXml(block, drawingId++);
                continue;
            }
            if (block.kind == DocxBlock::Kind::TextBox) {
                anchors += textBoxXml(block, drawingId++);
                continue;
            }
            if (block.kind != DocxBlock::Kind::Picture) continue;
            QByteArray png;
            const QString ext = encodePicture(block.picture, opt, &png);
            if (ext.isEmpty()) continue;
            MediaPart part;
            part.name  = QStringLiteral("image%1.%2").arg(mediaId).arg(ext);
            part.relId = QStringLiteral("rIdImg%1").arg(mediaId);
            part.png   = std::move(png);
            anchors += pictureXml(block, part.relId, drawingId++);
            media->append(std::move(part));
            ++mediaId;
        }

        QString body;
        if (pg > 0)
            body += QStringLiteral("<w:p><w:pPr><w:spacing w:before=\"0\" w:after=\"0\" "
                                   "w:line=\"20\" w:lineRule=\"exact\"/></w:pPr>"
                                   "<w:r><w:br w:type=\"page\"/></w:r></w:p>");
        if (!anchors.isEmpty())
            body += QStringLiteral("<w:p><w:pPr><w:spacing w:before=\"0\" w:after=\"0\" "
                                   "w:line=\"20\" w:lineRule=\"exact\"/>"
                                   "<w:rPr><w:sz w:val=\"2\"/></w:rPr></w:pPr>")
                  + anchors + QStringLiteral("</w:p>");

        // The cursor follows what Word will actually lay out, not the PDF's ink
        // extent. A 24 pt heading occupies a ~28 pt line box while its glyphs
        // measure 22 pt; charging the difference to the next gap keeps every
        // later block on the y it had in the PDF instead of drifting downwards.
        double cursor = section.top();
        for (const DocxBlock &block : page.blocks) {
            if (block.kind == DocxBlock::Kind::Picture
                    || block.kind == DocxBlock::Kind::Shape
                    || block.kind == DocxBlock::Kind::TextBox)
                continue;
            const double gap = block.bounds.top() - cursor;
            double rendered = 0.0;
            if (block.kind == DocxBlock::Kind::Table) {
                for (double h : block.table.rowHeightsPt) rendered += h;
            } else if (!block.lines.isEmpty()) {
                rendered = block.lines.size() * linePitchTwips(block) / 20.0;
            }
            if (block.kind == DocxBlock::Kind::Table) {
                // A table cannot carry space-before; an empty spacer paragraph
                // reproduces the gap the PDF had above it.
                if (gap > 1.0)
                    body += QStringLiteral("<w:p><w:pPr><w:spacing w:before=\"0\" "
                                           "w:after=\"0\" w:line=\"")
                          + twips(gap) + QStringLiteral("\" w:lineRule=\"exact\"/>"
                                                        "<w:rPr><w:sz w:val=\"2\"/>"
                                                        "</w:rPr></w:pPr></w:p>");
                DocxBlock shifted = block;
                shifted.table.indentPt += indentShift;
                body += tableXml(shifted);
            } else {
                DocxBlock shifted = block;
                shifted.indentPt += indentShift;
                body += paragraphXml(shifted, qMax(0.0, gap), false);
            }
            // A negative gap means the flow had already passed this block's
            // original top; it then starts wherever the cursor stands.
            cursor = qMax(block.bounds.top(), cursor) + rendered;
        }
        x += body;
    }

    const QSizeF size = pages.isEmpty() || pages.first().pageSizePt.isEmpty()
                            ? QSizeF(595.28, 841.89) : pages.first().pageSizePt;
    x += QStringLiteral("<w:sectPr><w:pgSz w:w=\"") + twips(size.width())
       + QStringLiteral("\" w:h=\"") + twips(size.height())
       + QStringLiteral("\"/><w:pgMar w:top=\"") + twips(section.top())
       + QStringLiteral("\" w:right=\"") + twips(section.right())
       + QStringLiteral("\" w:bottom=\"") + twips(section.bottom())
       + QStringLiteral("\" w:left=\"") + twips(section.left())
       + QStringLiteral("\" w:header=\"0\" w:footer=\"0\" w:gutter=\"0\"/>"
                        "</w:sectPr></w:body></w:document>");
    return x.toUtf8();
}

static QByteArray buildPositionedDocument(const QList<DocxPage> &pages,
                                          QList<MediaPart> *media,
                                          const DocxExportOptions &opt)
{
    QString x = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:v=\"urn:schemas-microsoft-com:vml\" "
        "xmlns:o=\"urn:schemas-microsoft-com:office:office\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<w:body>"
        "<w:p><w:r><w:pict><v:shapetype id=\"_x0000_t202\" coordsize=\"21600,21600\" "
        "o:spt=\"202\" path=\"m,l,21600r21600,l21600,xe\"><v:stroke joinstyle=\"miter\"/>"
        "<v:path gradientshapeok=\"t\" o:connecttype=\"rect\"/></v:shapetype>"
        "</w:pict></w:r></w:p>");

    int shapeId = 1;
    for (int pg = 0; pg < pages.size(); ++pg) {
        if (pg > 0)
            x += QStringLiteral("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>");

        // VML text boxes retain PDF coordinates while their contents remain
        // normal, editable WordprocessingML text.
        x += QStringLiteral("<w:p><w:pPr><w:spacing w:before=\"0\" w:after=\"0\"/>"
                            "</w:pPr>");
        QByteArray backgroundPng;
        // The scanned-page fallback carries the heaviest images in the whole
        // exporter, so it honours the quality setting just like the structured
        // path — leaving it on lossless PNG made the option look dead on
        // exactly the documents where it matters most.
        const QString bgExt = pages[pg].background.isNull()
            ? QString{} : encodePicture(pages[pg].background, opt, &backgroundPng);
        if (!bgExt.isEmpty()) {
            const QSizeF ps = pages[pg].pageSizePt.isEmpty()
                                  ? QSizeF(595.0, 842.0) : pages[pg].pageSizePt;
            media->append({ QStringLiteral("page%1.%2").arg(pg + 1).arg(bgExt),
                            QStringLiteral("rIdPage%1").arg(pg + 1),
                            backgroundPng });
            x += QStringLiteral("<w:r><w:pict><v:rect id=\"pdfBackground")
                 + QString::number(pg + 1)
                 + QStringLiteral("\" stroked=\"f\" style=\"position:absolute;left:0;top:0;")
                 + QStringLiteral("width:%1pt;height:%2pt;margin-left:0pt;margin-top:0pt;")
                       .arg(QString::number(ps.width(), 'f', 2),
                            QString::number(ps.height(), 'f', 2))
                 + QStringLiteral("mso-position-horizontal-relative:page;"
                                  "mso-position-vertical-relative:page;z-index:-251658240\">"
                                  "<v:imagedata r:id=\"rIdPage")
                 + QString::number(pg + 1)
                 + QStringLiteral("\" o:title=\"\"/></v:rect></w:pict></w:r>");
        }
        QList<ContentItem> textItems = pages[pg].items;
        std::stable_sort(textItems.begin(), textItems.end(),
                         [](const ContentItem &a, const ContentItem &b) {
            if (!qFuzzyCompare(a.bounds.top() + 1.0, b.bounds.top() + 1.0))
                return a.bounds.top() < b.bounds.top();
            return a.bounds.left() < b.bounds.left();
        });
        // Absolute positioning controls the appearance, while XML order
        // controls selection/copying and assistive reading. Providers put form
        // fields first for hit-testing priority, so restore visual reading
        // order here before serialising the editable boxes.
        for (const ContentItem &item : textItems) {
            if (!item.isTextual() || item.text.trimmed().isEmpty()) continue;
            const QRectF r = item.bounds.normalized();
            if (r.isEmpty()) continue;
            const double fontPt = item.fontSizePt > 0.0 ? item.fontSizePt : 10.0;
            const double pageW = pages[pg].pageSizePt.width() > 0.0
                                     ? pages[pg].pageSizePt.width() : 595.0;
            // Word's substitute-font metrics are commonly a few percent wider
            // than the embedded PDF font. A small right-side allowance avoids
            // clipped last letters without moving the original left edge.
            const double width = qMin(pageW - r.left(),
                                      r.width() + qMax(3.0, fontPt * 0.45));
            const double height = qMax(r.height() + qMax(3.0, fontPt * 0.30),
                                       fontPt * 1.30);
            x += QStringLiteral("<w:r><w:pict><v:shape id=\"pdfText")
                 + QString::number(shapeId++)
                 + QStringLiteral("\" o:spid=\"_x0000_s") + QString::number(shapeId + 1024)
                 + QStringLiteral("\" type=\"#_x0000_t202\" stroked=\"f\" style=\"")
                 + QStringLiteral("position:absolute;left:0;top:0;width:%1pt;height:%2pt;")
                       .arg(QString::number(qMax(1.0, width), 'f', 2),
                            QString::number(height, 'f', 2))
                 + QStringLiteral("margin-left:%1pt;margin-top:%2pt;")
                       .arg(QString::number(r.left(), 'f', 2),
                            QString::number(r.top(), 'f', 2))
                 + QStringLiteral("mso-position-horizontal-relative:page;"
                                  "mso-position-vertical-relative:page;z-index:1\"");
            if (pages[pg].background.isNull() && item.bgColor.isValid())
                x += QStringLiteral(" filled=\"t\" fillcolor=\"#") + colorHex(item.bgColor) + u'"';
            else
                x += QStringLiteral(" filled=\"f\"");
            // stroked="f" alone is not honoured by LibreOffice's VML import —
            // it drew a hairline frame around every single text box. The
            // explicit <v:stroke on="f"/> child is.
            x += QStringLiteral("><v:stroke on=\"f\"/>"
                                "<v:textbox inset=\"0,0,0,0\"><w:txbxContent>"
                                "<w:p><w:pPr><w:spacing w:before=\"0\" w:after=\"0\" "
                                "w:line=\"240\" w:lineRule=\"auto\"/></w:pPr>")
                 + textRuns(item)
                 + QStringLiteral("</w:p></w:txbxContent></v:textbox></v:shape>"
                                  "</w:pict></w:r>");
        }
        x += QStringLiteral("</w:p>");
    }

    const QSizeF size = pages.isEmpty() || pages.first().pageSizePt.isEmpty()
                            ? QSizeF(595.0, 842.0) : pages.first().pageSizePt;
    x += QStringLiteral("<w:sectPr><w:pgSz w:w=\"")
         + QString::number(qRound(size.width() * 20.0))
         + QStringLiteral("\" w:h=\"") + QString::number(qRound(size.height() * 20.0))
         + QStringLiteral("\"/><w:pgMar w:top=\"0\" w:right=\"0\" w:bottom=\"0\" "
                          "w:left=\"0\" w:header=\"0\" w:footer=\"0\" w:gutter=\"0\"/>"
                          "</w:sectPr></w:body></w:document>");
    return x.toUtf8();
}

// ── public API ────────────────────────────────────────────────────────────────

static bool writeDocx(const QString &outputPath, const QByteArray &documentXml,
                      const QList<MediaPart> &media)
{
    static const char kCT[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
        "<Override PartName=\"/word/document.xml\""
        " ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\""
        " ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        "</Types>";

    static const char kRels[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\""
        " Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\""
        " Target=\"word/document.xml\"/>"
        "</Relationships>";

    // References styles.xml so Word/LibreOffice find the style definitions
    QByteArray docRels = QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\""
        " Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\""
        " Target=\"styles.xml\"/>");
    for (const MediaPart &part : media)
        docRels += QStringLiteral(
            "<Relationship Id=\"%1\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
            "Target=\"media/%2\"/>").arg(part.relId, part.name).toUtf8();
    docRels += "</Relationships>";

    // docDefaults zeroes Word's own paragraph spacing — without it every
    // paragraph gains ~10 pt that the PDF never had and the page overflows.
    static const char kStyles[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:docDefaults>"
        "<w:rPrDefault><w:rPr>"
        "<w:rFonts w:ascii=\"Arial\" w:hAnsi=\"Arial\" w:cs=\"Arial\"/>"
        "<w:sz w:val=\"20\"/><w:szCs w:val=\"20\"/>"
        "</w:rPr></w:rPrDefault>"
        "<w:pPrDefault><w:pPr>"
        "<w:spacing w:before=\"0\" w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/>"
        "<w:widowControl w:val=\"0\"/>"
        "</w:pPr></w:pPrDefault>"
        "</w:docDefaults>"
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\">"
        "<w:name w:val=\"Normal\"/>"
        "</w:style>"
        "<w:style w:type=\"table\" w:default=\"1\" w:styleId=\"TableNormal\">"
        "<w:name w:val=\"Normal Table\"/>"
        "<w:tblPr><w:tblCellMar>"
        "<w:top w:w=\"0\" w:type=\"dxa\"/><w:left w:w=\"58\" w:type=\"dxa\"/>"
        "<w:bottom w:w=\"0\" w:type=\"dxa\"/><w:right w:w=\"58\" w:type=\"dxa\"/>"
        "</w:tblCellMar></w:tblPr>"
        "</w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Heading1\">"
        "<w:name w:val=\"heading 1\"/>"
        "<w:basedOn w:val=\"Normal\"/>"
        "<w:rPr><w:b/><w:sz w:val=\"32\"/></w:rPr>"
        "</w:style>"
        "</w:styles>";

    ZipWriter zip;
    zip.add("[Content_Types].xml",          QByteArray(kCT));
    zip.add("_rels/.rels",                  QByteArray(kRels));
    zip.add("word/_rels/document.xml.rels", docRels);
    zip.add("word/styles.xml",              QByteArray(kStyles));
    zip.add("word/document.xml",            documentXml);
    for (const MediaPart &part : media)
        zip.add(("word/media/" + part.name).toUtf8(), part.png);

    QFile f(outputPath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const QByteArray bytes = zip.archive();
    return f.write(bytes) == bytes.size();
}

bool DocxExporter::exportToDocx(const QString &outputPath,
                                const QList<DocxPage> &pages,
                                const QString &title,
                                const DocxExportOptions &options)
{
    Q_UNUSED(title);
    QList<MediaPart> media;

    // Native paragraphs/tables/shapes are the primary path. A page-sized
    // picture is reserved for scans or synthetic callers with no structure.
    const bool structured = std::any_of(pages.cbegin(), pages.cend(),
                                        [](const DocxPage &page) {
        return !page.blocks.isEmpty();
    });
    if (structured)
        return writeDocx(outputPath, buildStructuredDocument(pages, &media, options), media);

    if (prefersSemanticLayout(pages))
        return writeDocx(outputPath, buildSemanticDocument(pages), {});
    return writeDocx(outputPath, buildPositionedDocument(pages, &media, options), media);
}

bool DocxExporter::exportToDocx(const QString &outputPath,
                                const QList<QString> &pageTexts,
                                const QString &title)
{
    return writeDocx(outputPath, buildDocument(pageTexts, title), {});
}
