#include "DocxExporter.hpp"

#include <QByteArray>
#include <QBuffer>
#include <QFile>

#include <algorithm>
#include <cmath>

// ── minimal ZIP writer (store, no compression) ────────────────────────────────

static uint32_t crc32Compute(const QByteArray &data)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : data) {
        crc ^= c;
        for (int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}

static void u16le(QByteArray &b, uint16_t v)
{
    b += char(v & 0xFF);
    b += char((v >> 8) & 0xFF);
}
static void u32le(QByteArray &b, uint32_t v)
{
    b += char(v & 0xFF);
    b += char((v >> 8)  & 0xFF);
    b += char((v >> 16) & 0xFF);
    b += char((v >> 24) & 0xFF);
}

struct ZEntry {
    QByteArray name;
    QByteArray data;
    uint32_t   crc    { 0 };
    uint32_t   offset { 0 };
};

static void writeLocal(QByteArray &zip, ZEntry &e)
{
    e.crc    = crc32Compute(e.data);
    e.offset = static_cast<uint32_t>(zip.size());
    zip += "\x50\x4B\x03\x04";
    u16le(zip, 20); u16le(zip, 0); u16le(zip, 0);
    u16le(zip, 0);  u16le(zip, 0);
    u32le(zip, e.crc);
    u32le(zip, static_cast<uint32_t>(e.data.size()));
    u32le(zip, static_cast<uint32_t>(e.data.size()));
    u16le(zip, static_cast<uint16_t>(e.name.size()));
    u16le(zip, 0);
    zip += e.name;
    zip += e.data;
}

static void writeCentral(QByteArray &cd, const ZEntry &e)
{
    cd += "\x50\x4B\x01\x02";
    u16le(cd, 20); u16le(cd, 20); u16le(cd, 0); u16le(cd, 0);
    u16le(cd, 0);  u16le(cd, 0);
    u32le(cd, e.crc);
    u32le(cd, static_cast<uint32_t>(e.data.size()));
    u32le(cd, static_cast<uint32_t>(e.data.size()));
    u16le(cd, static_cast<uint16_t>(e.name.size()));
    u16le(cd, 0); u16le(cd, 0); u16le(cd, 0); u16le(cd, 0);
    u32le(cd, 0); u32le(cd, e.offset);
    cd += e.name;
}

// ── DOCX XML builders ─────────────────────────────────────────────────────────

static QString xmlEsc(const QString &s)
{
    QString r;
    r.reserve(s.size() + 8);
    for (QChar c : s) {
        const ushort u = c.unicode();
        if      (u == '&')  r += QLatin1String("&amp;");
        else if (u == '<')  r += QLatin1String("&lt;");
        else if (u == '>')  r += QLatin1String("&gt;");
        else if (u == '"')  r += QLatin1String("&quot;");
        else if (u < 32 && u != '\n' && u != '\r' && u != '\t') { /* skip */ }
        else if (u >= 0xD800 && u <= 0xDFFF) { /* surrogates: invalid in XML 1.0 */ }
        else if (u == 0xFFFE || u == 0xFFFF)  { /* non-characters: invalid in XML 1.0 */ }
        else                r += c;
    }
    return r;
}

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

static QString colorHex(const QColor &color)
{
    return color.isValid() ? color.name(QColor::HexRgb).mid(1).toUpper()
                           : QStringLiteral("000000");
}

static QString textRuns(const ContentItem &item)
{
    QString x;
    const int halfPoints = qMax(2, qRound((item.fontSizePt > 0.0
                                           ? item.fontSizePt : 10.0) * 2.0));
    const QString family = item.fontFamily.isEmpty()
                               ? QStringLiteral("Arial") : item.fontFamily;
    const QStringList lines = item.text.split(u'\n');
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0) x += QStringLiteral("<w:r><w:br/></w:r>");
        x += QStringLiteral("<w:r><w:rPr><w:rFonts w:ascii=\"")
             + xmlEsc(family)
             + QStringLiteral("\" w:hAnsi=\"") + xmlEsc(family)
             + QStringLiteral("\"/>");
        if (item.bold)   x += QStringLiteral("<w:b/>");
        if (item.italic) x += QStringLiteral("<w:i/>");
        x += QStringLiteral("<w:color w:val=\"") + colorHex(item.textColor)
             + QStringLiteral("\"/><w:sz w:val=\"") + QString::number(halfPoints)
             + QStringLiteral("\"/></w:rPr><w:t xml:space=\"preserve\">")
             + xmlEsc(lines[i]) + QStringLiteral("</w:t></w:r>");
    }
    return x;
}

static QString semanticTextRuns(const ContentItem &item)
{
    ContentItem adjusted = item;
    // Positioned boxes needed conservative metrics to avoid clipping. Native
    // Word paragraphs can reflow, so use the actual PDF glyph height and avoid
    // the undersized text visible in the semantic export.
    const double minimumPt = item.type == ContentItem::Type::TableCell ? 7.5 : 9.5;
    adjusted.fontSizePt = std::max({item.fontSizePt * 1.25,
                                    item.bounds.height() * 1.12,
                                    minimumPt});
    if (item.bounds.height() >= 11.0)
        adjusted.fontSizePt = qMax(adjusted.fontSizePt, 12.0);
    if (adjusted.text.trimmed().startsWith(QStringLiteral("•")))
        adjusted.fontSizePt = qMax(adjusted.fontSizePt, 8.0);
    return textRuns(adjusted);
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

static QString semanticParagraph(const ContentItem &item, int beforeTwips,
                                 int leftTwips = 0)
{
    return QStringLiteral("<w:p><w:pPr><w:spacing w:before=\"%1\" w:after=\"0\" "
                          "w:line=\"240\" w:lineRule=\"auto\"/>"
                          "<w:ind w:left=\"%2\"/></w:pPr>")
               .arg(qMax(0, beforeTwips)).arg(qMax(0, leftTwips))
         + semanticTextRuns(item) + QStringLiteral("</w:p>");
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

static QByteArray buildPositionedDocument(const QList<DocxPage> &pages)
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
        if (!pages[pg].background.isNull()) {
            const QSizeF ps = pages[pg].pageSizePt.isEmpty()
                                  ? QSizeF(595.0, 842.0) : pages[pg].pageSizePt;
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
        for (const ContentItem &item : pages[pg].items) {
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
            x += QStringLiteral("><v:textbox inset=\"0,0,0,0\"><w:txbxContent>"
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
                      const QList<DocxPage> &pages)
{
    static const char kCT[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
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
    for (int i = 0; i < pages.size(); ++i) {
        if (pages[i].background.isNull()) continue;
        docRels += QStringLiteral(
            "<Relationship Id=\"rIdPage%1\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
            "Target=\"media/page%1.png\"/>").arg(i + 1).toUtf8();
    }
    docRels += "</Relationships>";

    static const char kStyles[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\">"
        "<w:name w:val=\"Normal\"/>"
        "</w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Heading1\">"
        "<w:name w:val=\"heading 1\"/>"
        "<w:basedOn w:val=\"Normal\"/>"
        "<w:rPr><w:b/><w:sz w:val=\"32\"/></w:rPr>"
        "</w:style>"
        "</w:styles>";

    QList<ZEntry> entries;
    entries.append({"[Content_Types].xml",          QByteArray(kCT),      0, 0});
    entries.append({"_rels/.rels",                  QByteArray(kRels),    0, 0});
    entries.append({"word/_rels/document.xml.rels", docRels, 0, 0});
    entries.append({"word/styles.xml",              QByteArray(kStyles),  0, 0});
    entries.append({"word/document.xml",            documentXml, 0, 0});
    for (int i = 0; i < pages.size(); ++i) {
        if (pages[i].background.isNull()) continue;
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!pages[i].background.save(&buffer, "PNG")) return false;
        entries.append({QStringLiteral("word/media/page%1.png").arg(i + 1).toUtf8(),
                        std::move(png), 0, 0});
    }

    QByteArray zip;
    for (auto &e : entries)
        writeLocal(zip, e);

    QByteArray cd;
    for (const auto &e : entries)
        writeCentral(cd, e);

    const uint32_t cdOff  = static_cast<uint32_t>(zip.size());
    const uint32_t cdSize = static_cast<uint32_t>(cd.size());
    zip += cd;

    zip += "\x50\x4B\x05\x06";
    u16le(zip, 0); u16le(zip, 0);
    u16le(zip, static_cast<uint16_t>(entries.size()));
    u16le(zip, static_cast<uint16_t>(entries.size()));
    u32le(zip, cdSize);
    u32le(zip, cdOff);
    u16le(zip, 0);

    QFile f(outputPath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(zip) == zip.size();
}

bool DocxExporter::exportToDocx(const QString &outputPath,
                                const QList<DocxPage> &pages,
                                const QString &title)
{
    Q_UNUSED(title);
    if (prefersSemanticLayout(pages))
        return writeDocx(outputPath, buildSemanticDocument(pages), {});
    return writeDocx(outputPath, buildPositionedDocument(pages), pages);
}

bool DocxExporter::exportToDocx(const QString &outputPath,
                                const QList<QString> &pageTexts,
                                const QString &title)
{
    return writeDocx(outputPath, buildDocument(pageTexts, title), {});
}
