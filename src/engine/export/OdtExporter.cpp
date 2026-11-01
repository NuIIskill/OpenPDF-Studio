#include "engine/export/OdtExporter.hpp"
#include "engine/export/DocxXml.hpp"
#include "engine/export/ZipWriter.hpp"

#include <QFile>
#include <QHash>

#include <algorithm>

using namespace DocxXml;

namespace {

constexpr char kNamespaces[] =
    " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
    " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
    " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
    " xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\""
    " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\""
    " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
    " xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\""
    " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
    " xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\""
    " office:version=\"1.3\"";

QString pt(double value)
{
    return QString::number(value, 'f', 2) + QStringLiteral("pt");
}

QString odfColor(const QColor &color)
{
    return u'#' + colorHex(color).toLower();
}

QSizeF pageSize(const DocxPage &page)
{
    return page.pageSizePt.isEmpty() ? QSizeF(595.28, 841.89) : page.pageSizePt;
}

/// Text with the spaces, tabs and line breaks that ODF keeps only as elements.
QString odfText(const QString &text)
{
    QString out;
    QString plain;
    int  spaces     = 0;
    bool afterSpace = true;   // a paragraph drops its leading spaces
    const auto flush = [&] {
        out += xmlEsc(plain);
        plain.clear();
        if (spaces == 1) out += QStringLiteral("<text:s/>");
        else if (spaces > 1) out += QStringLiteral("<text:s text:c=\"%1\"/>").arg(spaces);
        spaces = 0;
    };
    for (const QChar ch : text) {
        if (ch == u' ') {
            if (afterSpace) ++spaces;
            else            { plain += ch; afterSpace = true; }
            continue;
        }
        if (ch == u'\r') continue;
        if (spaces > 0) flush();
        afterSpace = false;
        if (ch == u'\t') {
            flush();
            out += QStringLiteral("<text:tab/>");
        } else if (ch == u'\n') {
            flush();
            out += QStringLiteral("<text:line-break/>");
            afterSpace = true;
        } else {
            plain += ch;
        }
    }
    flush();
    return out;
}

QString textProperties(const ContentItem &item)
{
    const QString family = item.fontFamily.isEmpty() || isSymbolFont(item.fontFamily)
                               ? QStringLiteral("Arial") : item.fontFamily;
    const QString quoted = u'\'' + xmlEsc(family) + u'\'';
    const QString size = pt(item.fontSizePt > 0.0 ? item.fontSizePt : 10.0);
    QString x = QStringLiteral("<style:text-properties fo:font-family=\"%1\" "
                               "style:font-family-asian=\"%1\" style:font-family-complex=\"%1\" "
                               "fo:font-size=\"%2\" style:font-size-asian=\"%2\" "
                               "style:font-size-complex=\"%2\" fo:color=\"%3\"")
                    .arg(quoted, size, odfColor(item.textColor));
    if (item.bold)
        x += QStringLiteral(" fo:font-weight=\"bold\" style:font-weight-asian=\"bold\" "
                            "style:font-weight-complex=\"bold\"");
    if (item.italic)
        x += QStringLiteral(" fo:font-style=\"italic\" style:font-style-asian=\"italic\" "
                            "style:font-style-complex=\"italic\"");
    return x + QStringLiteral("/>");
}

QString pageAnchor(int pageNumber, const QRectF &rect, int z)
{
    return QStringLiteral("text:anchor-type=\"page\" text:anchor-page-number=\"%1\" "
                          "svg:x=\"%2\" svg:y=\"%3\" svg:width=\"%4\" svg:height=\"%5\" "
                          "draw:z-index=\"%6\"")
        .arg(QString::number(pageNumber), pt(rect.left()), pt(rect.top()),
             pt(qMax(0.2, rect.width())), pt(qMax(0.2, rect.height())), QString::number(z));
}

/// Builds content.xml and styles.xml; every look is an automatic style, shared by all that use it.
class OdtDocument
{
public:
    void addPage(const DocxPage &page, int pageNumber);
    QByteArray contentXml() const;
    QByteArray stylesXml() const;
    const QList<MediaPart> &media() const { return m_media; }

private:
    QString style(const QString &family, QStringView prefix, const QString &attributes,
                  const QString &body);
    QString masterFor(const DocxPage &page);
    QString span(const ContentItem &item, const QString &text);
    QString paragraph(const DocxBlock &block, double spaceBeforePt, const QString &master);
    QString cell(const DocxCell *cell, const DocxTable &table, int row, int col, int span,
                 double lead);
    QString table(const DocxBlock &block, double spaceBeforePt, const QString &master);
    void picture(const QRectF &rect, const DocxImage &image, int pageNumber);
    void shape(const DocxBlock &block, int pageNumber);
    void textFrame(const ContentItem &item, const QRectF &fallback, Qt::Alignment align,
                   const QColor &fill, int pageNumber);
    void flow(const DocxPage &page, const QString &master);
    void positioned(const DocxPage &page, int pageNumber);

    QHash<QString, QString> m_styleNames;
    QHash<QString, int>     m_prefixCount;
    QString m_automatic;
    QString m_pageLayouts;
    QString m_masters;
    QHash<QString, QString> m_masterNames;
    QString m_frames;
    QString m_body;
    QList<MediaPart> m_media;
    int m_z      { 0 };
    int m_tables { 0 };
};

QString OdtDocument::style(const QString &family, QStringView prefix,
                           const QString &attributes, const QString &body)
{
    const QString key = family + attributes + body;
    if (const auto it = m_styleNames.constFind(key); it != m_styleNames.cend()) return *it;
    const QString name = prefix + QString::number(++m_prefixCount[prefix.toString()]);
    m_styleNames.insert(key, name);
    m_automatic += QStringLiteral("<style:style style:name=\"%1\" style:family=\"%2\"%3>")
                       .arg(name, family, attributes)
                 + body + QStringLiteral("</style:style>");
    return name;
}

QString OdtDocument::masterFor(const DocxPage &page)
{
    const QSizeF size = pageSize(page);
    const QMarginsF &m = page.marginsPt;
    const QString properties =
        QStringLiteral("<style:page-layout-properties fo:page-width=\"%1\" fo:page-height=\"%2\" "
                       "style:print-orientation=\"%3\" fo:margin-top=\"%4\" "
                       "fo:margin-bottom=\"%5\" fo:margin-left=\"%6\" fo:margin-right=\"%7\"/>")
            .arg(pt(size.width()), pt(size.height()),
                 size.width() > size.height() ? QStringLiteral("landscape")
                                              : QStringLiteral("portrait"),
                 pt(m.top()), pt(m.bottom()), pt(m.left()), pt(m.right()));
    if (const auto it = m_masterNames.constFind(properties); it != m_masterNames.cend())
        return *it;
    const int n = m_masterNames.size() + 1;
    const QString name = QStringLiteral("Page%1").arg(n);
    m_masterNames.insert(properties, name);
    m_pageLayouts += QStringLiteral("<style:page-layout style:name=\"pm%1\">").arg(n)
                   + properties + QStringLiteral("</style:page-layout>");
    m_masters += QStringLiteral("<style:master-page style:name=\"%1\" "
                                "style:page-layout-name=\"pm%2\"/>").arg(name).arg(n);
    return name;
}

QString OdtDocument::span(const ContentItem &item, const QString &text)
{
    const QString name = style(QStringLiteral("text"), u"T", {}, textProperties(item));
    return QStringLiteral("<text:span text:style-name=\"%1\">").arg(name) + odfText(text)
         + QStringLiteral("</text:span>");
}

QString OdtDocument::paragraph(const DocxBlock &block, double spaceBeforePt,
                               const QString &master)
{
    // As in Word: the first line that is not in a symbol font gives the look.
    const ContentItem *pick = &block.lines.first();
    for (const ContentItem &line : block.lines) {
        if (isSymbolFont(line.fontFamily)) continue;
        pick = &line;
        break;
    }
    QString properties = QStringLiteral("<style:paragraph-properties fo:margin-top=\"%1\" "
                                        "fo:margin-bottom=\"0pt\" fo:line-height=\"%2\" "
                                        "fo:text-align=\"%3\"")
                             .arg(pt(spaceBeforePt), pt(linePitchTwips(block) / 20.0),
                                  alignValue(block.align));
    if (block.indentPt + block.hangingPt > 1.0)
        properties += QStringLiteral(" fo:margin-left=\"%1\"")
                          .arg(pt(block.indentPt + block.hangingPt));
    if (block.hangingPt > 0.0)
        properties += QStringLiteral(" fo:text-indent=\"%1\"").arg(pt(-block.hangingPt));
    if (block.rightIndentPt > 0.0)
        properties += QStringLiteral(" fo:margin-right=\"%1\"").arg(pt(block.rightIndentPt));
    if (pick->bgColor.isValid() && pick->bgColor != QColor(Qt::white))
        properties += QStringLiteral(" fo:background-color=\"%1\"").arg(odfColor(pick->bgColor));
    // Tabs count from the paragraph's indent, where a list's text begins.
    QList<double> stops;
    if (block.hangingPt > 0.0) stops.append(0.0);
    for (double stop : tabStops(block)) stops.append(stop - block.hangingPt);
    if (stops.isEmpty()) {
        properties += QStringLiteral("/>");
    } else {
        properties += QStringLiteral("><style:tab-stops>");
        for (double stop : std::as_const(stops))
            properties += QStringLiteral("<style:tab-stop style:position=\"%1\"/>").arg(pt(stop));
        properties += QStringLiteral("</style:tab-stops></style:paragraph-properties>");
    }

    const QString attributes = master.isEmpty() ? QString()
        : QStringLiteral(" style:master-page-name=\"%1\"").arg(master);
    const QString name = style(QStringLiteral("paragraph"), u"P", attributes, properties);
    QString spans;
    for (const Span &piece : paragraphSpans(block)) spans += span(piece.style, piece.text);
    return QStringLiteral("<text:p text:style-name=\"%1\">").arg(name)
         + spans + QStringLiteral("</text:p>");
}

QString OdtDocument::cell(const DocxCell *cell, const DocxTable &table, int row, int col, int span,
                          double lead)
{
    // Cells on the table's edge carry its frame, the others its inner rules;
    // a rule between two cells is drawn by the one below or to the right only.
    const int cols = int(table.colWidthsPt.size());
    const auto rule = [&table](bool edge) {
        return QStringLiteral("%1 solid %2")
            .arg(pt(edge ? table.ruleOuterPt : table.ruleInnerPt), odfColor(table.ruleColor));
    };
    const QString borders = !table.hasBorders ? QStringLiteral("fo:border=\"none\"")
        : QStringLiteral("fo:border-top=\"%1\" fo:border-bottom=\"%2\" "
                         "fo:border-left=\"%3\" fo:border-right=\"%4\"")
              .arg(rule(row == 0),
                   row == table.rowCount - 1 ? rule(true) : QStringLiteral("none"),
                   rule(col == 0),
                   col + span >= cols ? rule(true) : QStringLiteral("none"));
    QString properties = QStringLiteral("<style:table-cell-properties "
                                        "style:vertical-align=\"%4\" fo:padding-top=\"0pt\" "
                                        "fo:padding-bottom=\"0pt\" fo:padding-left=\"%2\" "
                                        "fo:padding-right=\"%3\" %1")
                             .arg(borders, pt(table.padLeftPt), pt(table.padRightPt),
                                  lead >= 0.0 ? QStringLiteral("top") : QStringLiteral("middle"));
    if (cell && cell->shading.isValid() && cell->shading != QColor(Qt::white))
        properties += QStringLiteral(" fo:background-color=\"%1\"").arg(odfColor(cell->shading));
    properties += QStringLiteral("/>");
    const QString name = style(QStringLiteral("table-cell"), u"Tc", {}, properties);

    QString x = QStringLiteral("<table:table-cell table:style-name=\"%1\" "
                               "office:value-type=\"string\"").arg(name);
    if (cell && cell->colSpan > 1)
        x += QStringLiteral(" table:number-columns-spanned=\"%1\"").arg(cell->colSpan);
    x += u'>';
    if (cell && !cell->item.text.trimmed().isEmpty()) {
        DocxBlock content;
        content.lines = { cell->item };
        content.align = cell->align;
        x += paragraph(content, qMax(0.0, lead), {});
    } else {
        x += QStringLiteral("<text:p/>");
    }
    return x + QStringLiteral("</table:table-cell>");
}

QString OdtDocument::table(const DocxBlock &block, double spaceBeforePt, const QString &master)
{
    const DocxTable &t = block.table;
    const int cols = t.colWidthsPt.size();
    if (cols == 0 || t.rowCount == 0) return {};

    double total = 0.0;
    for (double w : t.colWidthsPt) total += w;
    QString attributes = master.isEmpty() ? QString()
        : QStringLiteral(" style:master-page-name=\"%1\"").arg(master);
    const QString tableStyle = style(
        QStringLiteral("table"), u"Ta", attributes,
        QStringLiteral("<style:table-properties style:width=\"%1\" table:align=\"left\" "
                       "fo:margin-left=\"%2\" fo:margin-top=\"%3\" fo:margin-bottom=\"0pt\"/>")
            .arg(pt(total), pt(qMax(0.0, t.indentPt)),
                 pt(spaceBeforePt > 1.0 ? spaceBeforePt : 0.0)));

    QString x = QStringLiteral("<table:table table:name=\"Table%1\" table:style-name=\"%2\">")
                    .arg(++m_tables).arg(tableStyle);
    for (double w : t.colWidthsPt)
        x += QStringLiteral("<table:table-column table:style-name=\"%1\"/>")
                 .arg(style(QStringLiteral("table-column"), u"Tk", {},
                            QStringLiteral("<style:table-column-properties "
                                           "style:column-width=\"%1\"/>").arg(pt(w))));

    for (int r = 0; r < t.rowCount; ++r) {
        QString rowStyle;
        if (r < t.rowHeightsPt.size())
            rowStyle = QStringLiteral(" table:style-name=\"%1\"")
                           .arg(style(QStringLiteral("table-row"), u"Tr", {},
                                      QStringLiteral("<style:table-row-properties "
                                                     "style:min-row-height=\"%1\"/>")
                                          .arg(pt(t.rowHeightsPt[r]))));
        x += QStringLiteral("<table:table-row%1>").arg(rowStyle);
        for (int col = 0; col < cols;) {
            const auto found = std::find_if(t.cells.cbegin(), t.cells.cend(),
                                            [r, col](const DocxCell &c) {
                return c.row == r && c.col == col;
            });
            const DocxCell *c = found != t.cells.cend() ? &*found : nullptr;
            const int spanCols = c ? qBound(1, c->colSpan, cols - col) : 1;
            DocxCell clamped;
            if (c) { clamped = *c; clamped.colSpan = spanCols; }
            x += cell(c ? &clamped : nullptr, t, r, col, spanCols,
                      c ? cellLead(block, r, c->item) : -1.0);
            for (int s = 1; s < spanCols; ++s) x += QStringLiteral("<table:covered-table-cell/>");
            col += spanCols;
        }
        x += QStringLiteral("</table:table-row>");
    }
    return x + QStringLiteral("</table:table>");
}

void OdtDocument::picture(const QRectF &rect, const DocxImage &image, int pageNumber)
{
    const QString name = QStringLiteral("image%1.%2").arg(m_media.size() + 1).arg(image.ext);
    m_media.append({ name, {}, image.data });
    const QString frameStyle = style(
        QStringLiteral("graphic"), u"fr", {},
        QStringLiteral("<style:graphic-properties style:wrap=\"run-through\" "
                       "style:run-through=\"background\" style:vertical-pos=\"from-top\" "
                       "style:vertical-rel=\"page\" style:horizontal-pos=\"from-left\" "
                       "style:horizontal-rel=\"page\" fo:padding=\"0pt\" fo:border=\"none\" "
                       "draw:stroke=\"none\" draw:fill=\"none\"/>"));
    m_frames += QStringLiteral("<draw:frame draw:style-name=\"%1\" draw:name=\"Image%2\" ")
                    .arg(frameStyle).arg(m_media.size())
              + pageAnchor(pageNumber, rect, m_z++)
              + QStringLiteral("><draw:image xlink:href=\"Pictures/%1\" xlink:type=\"simple\" "
                               "xlink:show=\"embed\" xlink:actuate=\"onLoad\"/></draw:frame>")
                    .arg(name);
}

void OdtDocument::shape(const DocxBlock &block, int pageNumber)
{
    const QColor fill = block.fillColor.isValid() ? block.fillColor : QColor(Qt::transparent);
    const bool stroked = block.strokeColor.isValid() && block.strokeWidthPt > 0.0;
    QString properties = QStringLiteral("<style:graphic-properties style:wrap=\"run-through\" "
                                        "style:run-through=\"background\" "
                                        "style:vertical-pos=\"from-top\" "
                                        "style:vertical-rel=\"page\" "
                                        "style:horizontal-pos=\"from-left\" "
                                        "style:horizontal-rel=\"page\" draw:shadow=\"hidden\"");
    properties += fill.alpha() > 0
        ? QStringLiteral(" draw:fill=\"solid\" draw:fill-color=\"%1\"").arg(odfColor(fill))
        : QStringLiteral(" draw:fill=\"none\"");
    properties += stroked
        ? QStringLiteral(" draw:stroke=\"solid\" svg:stroke-color=\"%1\" svg:stroke-width=\"%2\"")
              .arg(odfColor(block.strokeColor), pt(block.strokeWidthPt))
        : QStringLiteral(" draw:stroke=\"none\"");
    properties += QStringLiteral("/>");
    m_frames += QStringLiteral("<draw:rect draw:style-name=\"%1\" ")
                    .arg(style(QStringLiteral("graphic"), u"gr", {}, properties))
              + pageAnchor(pageNumber, block.bounds, m_z++) + QStringLiteral("/>");
}

void OdtDocument::textFrame(const ContentItem &item, const QRectF &fallback, Qt::Alignment align,
                            const QColor &fill, int pageNumber)
{
    double line = 0.0;
    const QRectF rect = textBoxRect(item, fallback, &line);
    const QString frameStyle = style(
        QStringLiteral("graphic"), u"fr", {},
        QStringLiteral("<style:graphic-properties style:wrap=\"run-through\" "
                       "style:run-through=\"foreground\" style:vertical-pos=\"from-top\" "
                       "style:vertical-rel=\"page\" style:horizontal-pos=\"from-left\" "
                       "style:horizontal-rel=\"page\" fo:padding=\"0pt\" fo:border=\"none\" "
                       "draw:stroke=\"none\" ")
            + (fill.isValid() ? QStringLiteral("draw:fill=\"solid\" draw:fill-color=\"%1\"/>")
                                    .arg(odfColor(fill))
                              : QStringLiteral("draw:fill=\"none\"/>")));
    const QString paragraphStyle = style(
        QStringLiteral("paragraph"), u"P", {},
        QStringLiteral("<style:paragraph-properties fo:margin-top=\"0pt\" fo:margin-bottom=\"0pt\" "
                       "fo:line-height=\"%2\" fo:text-align=\"%1\"/>")
            .arg(alignValue(align), pt(line)));
    // The box grows with its text instead of cutting it off when a substitute font runs wider.
    m_frames += QStringLiteral("<draw:frame draw:style-name=\"%1\" ").arg(frameStyle)
              + pageAnchor(pageNumber, rect, m_z++)
              + QStringLiteral("><draw:text-box fo:min-height=\"%1\" fo:min-width=\"%2\">"
                               "<text:p text:style-name=\"%3\">")
                    .arg(pt(rect.height()), pt(rect.width()), paragraphStyle)
              + span(item, withoutSymbolFont(item.text))
              + QStringLiteral("</text:p></draw:text-box></draw:frame>");
}

void OdtDocument::flow(const DocxPage &page, const QString &master)
{
    QString opener = master;
    double cursor = page.marginsPt.top();
    for (const DocxBlock &block : page.blocks) {
        const bool isTable = block.kind == DocxBlock::Kind::Table;
        if (!isTable && (block.kind != DocxBlock::Kind::Paragraph || block.lines.isEmpty()))
            continue;
        const double top = flowTop(block);
        const double gap = qMax(0.0, top - cursor);
        double rendered = 0.0;
        if (isTable) {
            for (double h : block.table.rowHeightsPt) rendered += h;
            m_body += table(block, gap, opener);
        } else {
            rendered = block.lines.size() * linePitchTwips(block) / 20.0;
            m_body += paragraph(block, gap, opener);
        }
        opener.clear();
        cursor = qMax(top, cursor) + rendered;
    }
    // A page with nothing in the flow still needs a paragraph to open it.
    if (!opener.isEmpty())
        m_body += QStringLiteral("<text:p text:style-name=\"%1\"/>")
                      .arg(style(QStringLiteral("paragraph"), u"P",
                                 QStringLiteral(" style:master-page-name=\"%1\"").arg(opener),
                                 QStringLiteral("<style:paragraph-properties fo:margin-top=\"0pt\" "
                                                "fo:margin-bottom=\"0pt\" fo:line-height=\"1pt\"/>"
                                                "<style:text-properties fo:font-size=\"1pt\"/>")));
}

void OdtDocument::positioned(const DocxPage &page, int pageNumber)
{
    const QSizeF size = pageSize(page);
    if (!page.background.isNull())
        picture(QRectF(QPointF(0.0, 0.0), size), page.background, pageNumber);

    QList<ContentItem> items = page.items;
    std::stable_sort(items.begin(), items.end(), [](const ContentItem &a, const ContentItem &b) {
        if (!qFuzzyCompare(a.bounds.top() + 1.0, b.bounds.top() + 1.0))
            return a.bounds.top() < b.bounds.top();
        return a.bounds.left() < b.bounds.left();
    });
    for (const ContentItem &item : items) {
        if (!item.isTextual() || item.text.trimmed().isEmpty()) continue;
        const QRectF r = item.bounds.normalized();
        if (r.isEmpty()) continue;
        const double fontPt = item.fontSizePt > 0.0 ? item.fontSizePt : 10.0;
        const double width  = qMin(size.width() - r.left(), r.width() + qMax(3.0, fontPt * 0.45));
        const double height = qMax(r.height() + qMax(3.0, fontPt * 0.30), fontPt * 1.30);
        const QColor fill = page.background.isNull() ? item.bgColor : QColor();
        textFrame(item, QRectF(r.topLeft(), QSizeF(qMax(1.0, width), height)),
                  Qt::AlignLeft, fill, pageNumber);
    }
}

void OdtDocument::addPage(const DocxPage &page, int pageNumber)
{
    const QString master = masterFor(page);
    if (page.blocks.isEmpty()) {
        positioned(page, pageNumber);
        flow(page, master);
        return;
    }
    for (const DocxBlock &block : page.blocks) {
        if (block.kind == DocxBlock::Kind::Picture && !block.picture.isNull()) {
            picture(block.bounds, block.picture, pageNumber);
        } else if (block.kind == DocxBlock::Kind::Shape) {
            shape(block, pageNumber);
        } else if (block.kind == DocxBlock::Kind::TextBox && !block.lines.isEmpty()) {
            textFrame(block.lines.first(), block.bounds, block.align, {}, pageNumber);
        }
    }
    flow(page, master);
}

QByteArray OdtDocument::contentXml() const
{
    return (QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                           "<office:document-content") + QLatin1String(kNamespaces)
            + QStringLiteral("><office:automatic-styles>") + m_automatic
            + QStringLiteral("</office:automatic-styles><office:body><office:text>")
            + m_frames + m_body
            + QStringLiteral("</office:text></office:body></office:document-content>"))
        .toUtf8();
}

QByteArray OdtDocument::stylesXml() const
{
    return (QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                           "<office:document-styles") + QLatin1String(kNamespaces)
            + QStringLiteral("><office:styles>"
                             "<style:default-style style:family=\"paragraph\">"
                             "<style:paragraph-properties fo:margin-top=\"0pt\" "
                             "fo:margin-bottom=\"0pt\" fo:orphans=\"0\" fo:widows=\"0\"/>"
                             "<style:text-properties fo:font-family=\"Arial\" "
                             "fo:font-size=\"10pt\" style:font-size-asian=\"10pt\" "
                             "style:font-size-complex=\"10pt\"/></style:default-style>"
                             "<style:style style:name=\"Standard\" style:family=\"paragraph\" "
                             "style:class=\"text\"/></office:styles><office:automatic-styles>")
            + m_pageLayouts + QStringLiteral("</office:automatic-styles><office:master-styles>")
            + m_masters
            + QStringLiteral("</office:master-styles></office:document-styles>"))
        .toUtf8();
}

QByteArray manifestXml(const QList<MediaPart> &media)
{
    QString x = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" "
        "manifest:version=\"1.3\">"
        "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" "
        "manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
        "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
        "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>");
    for (const MediaPart &part : media)
        x += QStringLiteral("<manifest:file-entry manifest:full-path=\"Pictures/%1\" "
                            "manifest:media-type=\"image/%2\"/>")
                 .arg(part.name, part.name.section(u'.', -1));
    return (x + QStringLiteral("</manifest:manifest>")).toUtf8();
}

}

bool OdtExporter::exportToOdt(const QString &outputPath, const QList<DocxPage> &pages)
{
    OdtDocument document;
    for (int i = 0; i < pages.size(); ++i)
        document.addPage(pages[i], i + 1);

    static const char kMeta[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<office:document-meta xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" office:version=\"1.3\">"
        "<office:meta><meta:generator>OpenPDF Studio</meta:generator></office:meta>"
        "</office:document-meta>";

    // The media type has to be the first entry and stay uncompressed; it is
    // short enough that ZipWriter stores it as it is.
    ZipWriter zip;
    zip.add("mimetype", QByteArray("application/vnd.oasis.opendocument.text"));
    zip.add("META-INF/manifest.xml", manifestXml(document.media()));
    zip.add("meta.xml",    QByteArray(kMeta));
    zip.add("styles.xml",  document.stylesXml());
    zip.add("content.xml", document.contentXml());
    for (const MediaPart &part : document.media())
        zip.add(("Pictures/" + part.name).toUtf8(), part.png);

    QFile f(outputPath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = zip.archive();
    return f.write(bytes) == bytes.size();
}
