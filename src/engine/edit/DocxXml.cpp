#include "engine/edit/DocxXml.hpp"

#include <QBuffer>
#include <QFontMetricsF>
#include <QPainter>
#include <QtMath>

namespace DocxXml {

// English Metric Units — the unit every DrawingML length is expressed in.
constexpr double kEmuPerPt = 12700.0;

QString emu(double pt)
{
    return QString::number(qRound64(qMax(0.0, pt) * kEmuPerPt));
}

QString twips(double pt)
{
    return QString::number(qRound(pt * 20.0));
}

bool encodePng(const QImage &image, QByteArray *out)
{
    QBuffer buffer(out);
    buffer.open(QIODevice::WriteOnly);
    return !image.isNull() && image.save(&buffer, "PNG");
}

// Pictures are cropped from a 2x raster so the layout analysis always has the
// detail it needs. What the quality setting changes is what actually lands in
// the file: how far the picture is scaled back down, and whether it is stored
// lossless or as JPEG. Returns the file extension used.
QString encodePicture(const QImage &image, const DocxExportOptions &opt,
                             QByteArray *out)
{
    if (image.isNull()) return {};

    // 85 keeps the source resolution; below that the picture shrinks with it.
    const double factor = opt.imageQuality >= 95 ? 1.25
                        : opt.imageQuality >= 80 ? 1.0
                        : opt.imageQuality >= 55 ? 0.75 : 0.5;
    QImage scaled = image;
    if (!qFuzzyCompare(factor, 1.0)) {
        const QSize target(qMax(1, qRound(image.width()  * factor)),
                           qMax(1, qRound(image.height() * factor)));
        scaled = image.scaled(target, Qt::IgnoreAspectRatio,
                              Qt::SmoothTransformation);
    }

    QByteArray png;
    QBuffer pngBuffer(&png);
    pngBuffer.open(QIODevice::WriteOnly);
    if (!scaled.save(&pngBuffer, "PNG")) return {};

    if (opt.compressImages) {
        // JPEG has no alpha; the pictures are opaque page crops, but compose
        // over white so a stray alpha channel cannot turn into black.
        QImage opaque(scaled.size(), QImage::Format_RGB32);
        opaque.fill(Qt::white);
        QPainter p(&opaque);
        p.drawImage(0, 0, scaled);
        p.end();
        QByteArray jpeg;
        QBuffer jpegBuffer(&jpeg);
        jpegBuffer.open(QIODevice::WriteOnly);
        if (opaque.save(&jpegBuffer, "JPEG", qBound(10, opt.imageQuality, 100))
                && jpeg.size() < png.size()) {
            *out = std::move(jpeg);
            return QStringLiteral("jpeg");
        }
    }
    // Flat PDF graphics (rules, fills and charts) are commonly both smaller
    // and sharper as PNG. "Compress images" therefore permits JPEG but does
    // not force a visibly degraded encoding when lossless is the better fit.
    *out = std::move(png);
    return QStringLiteral("png");
}

QString xmlEsc(const QString &s)
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

QString colorHex(const QColor &color)
{
    return color.isValid() ? color.name(QColor::HexRgb).mid(1).toUpper()
                           : QStringLiteral("000000");
}

double fontSizeOf_(const ContentItem &item)
{
    return item.fontSizePt > 0.0 ? item.fontSizePt
                                 : qMax(6.0, item.bounds.height() * 0.9);
}

QString textRuns(const ContentItem &item)
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

QString semanticTextRuns(const ContentItem &item)
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

QString semanticParagraph(const ContentItem &item, int beforeTwips,
                                 int leftTwips)
{
    return QStringLiteral("<w:p><w:pPr><w:spacing w:before=\"%1\" w:after=\"0\" "
                          "w:line=\"240\" w:lineRule=\"auto\"/>"
                          "<w:ind w:left=\"%2\"/></w:pPr>")
               .arg(qMax(0, beforeTwips)).arg(qMax(0, leftTwips))
         + semanticTextRuns(item) + QStringLiteral("</w:p>");
}

QString paragraphText(const QList<ContentItem> &lines)
{
    QString out;
    for (const ContentItem &line : lines) {
        const QString t = line.text.trimmed();
        if (t.isEmpty()) continue;
        if (out.isEmpty()) { out = t; continue; }
        if (out.endsWith(u'-') && t.at(0).isLower()) {
            out.chop(1);
            out += t;
        } else {
            out += u' ' + t;
        }
    }
    return out;
}

QString runProperties(const ContentItem &style)
{
    const QString family = style.fontFamily.isEmpty() ? QStringLiteral("Arial")
                                                      : style.fontFamily;
    const int half = qMax(2, qRound((style.fontSizePt > 0.0 ? style.fontSizePt
                                                            : 10.0) * 2.0));
    QString x = QStringLiteral("<w:rPr><w:rFonts w:ascii=\"") + xmlEsc(family)
              + QStringLiteral("\" w:hAnsi=\"") + xmlEsc(family)
              + QStringLiteral("\" w:cs=\"") + xmlEsc(family) + QStringLiteral("\"/>");
    if (style.bold)   x += QStringLiteral("<w:b/>");
    if (style.italic) x += QStringLiteral("<w:i/>");
    x += QStringLiteral("<w:color w:val=\"") + colorHex(style.textColor)
       + QStringLiteral("\"/><w:sz w:val=\"") + QString::number(half)
       + QStringLiteral("\"/><w:szCs w:val=\"") + QString::number(half)
       + QStringLiteral("\"/></w:rPr>");
    return x;
}

QString alignValue(Qt::Alignment align)
{
    if (align & Qt::AlignHCenter) return QStringLiteral("center");
    if (align & Qt::AlignRight)   return QStringLiteral("right");
    return QStringLiteral("left");
}

// Line height is pinned to the pitch measured in the PDF. Left on "auto", Word
// applies the substituted font's own leading and every block drifts a little
// further down the page than the original.
int linePitchTwips(const DocxBlock &block)
{
    const ContentItem &first = block.lines.first();
    double pitch = fontSizeOf_(first) * 1.18;
    if (block.lines.size() > 1) {
        QList<double> gaps;
        for (int i = 1; i < block.lines.size(); ++i)
            gaps.append(block.lines[i].bounds.top() - block.lines[i - 1].bounds.top());
        std::sort(gaps.begin(), gaps.end());
        const double measured = gaps[gaps.size() / 2];
        if (measured > 1.0) pitch = measured;
    }
    return qRound(qMax(pitch, fontSizeOf_(first) * 1.05) * 20.0);
}

QString paragraphXml(const DocxBlock &block, double spaceBeforePt,
                            bool insideCell)
{
    if (block.lines.isEmpty()) return {};
    const ContentItem &style = block.lines.first();

    QString x = QStringLiteral("<w:p><w:pPr><w:spacing w:before=\"")
              + twips(qMax(0.0, spaceBeforePt))
              + QStringLiteral("\" w:after=\"0\" w:line=\"")
              + QString::number(linePitchTwips(block))
              + QStringLiteral("\" w:lineRule=\"exact\"/>");
    if (!insideCell && block.indentPt > 1.0)
        x += QStringLiteral("<w:ind w:left=\"") + twips(block.indentPt)
           + QStringLiteral("\"/>");
    x += QStringLiteral("<w:jc w:val=\"") + alignValue(block.align)
       + QStringLiteral("\"/>");
    // Shading carries the panel fill the layout pass found behind this text.
    if (style.bgColor.isValid() && style.bgColor != QColor(Qt::white))
        x += QStringLiteral("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"")
           + colorHex(style.bgColor) + QStringLiteral("\"/>");
    x += QStringLiteral("</w:pPr><w:r>") + runProperties(style)
       + QStringLiteral("<w:t xml:space=\"preserve\">")
       + xmlEsc(paragraphText(block.lines))
       + QStringLiteral("</w:t></w:r></w:p>");
    return x;
}

QString tableXml(const DocxBlock &block)
{
    const DocxTable &t = block.table;
    const int cols = t.colWidthsPt.size();
    if (cols == 0 || t.rowCount == 0) return {};

    double total = 0.0;
    for (double w : t.colWidthsPt) total += w;

    QString x = QStringLiteral("<w:tbl><w:tblPr><w:tblW w:w=\"") + twips(total)
              + QStringLiteral("\" w:type=\"dxa\"/>");
    if (t.indentPt > 1.0)
        x += QStringLiteral("<w:tblInd w:w=\"") + twips(t.indentPt)
           + QStringLiteral("\" w:type=\"dxa\"/>");
    // Cell margins are set on the table itself, not left to the default style:
    // LibreOffice applies its own ~5 pt top/bottom otherwise, every row grows
    // past the height asked for, and the whole page below the table shifts.
    x += QStringLiteral("<w:tblLayout w:type=\"fixed\"/>"
                        "<w:tblCellMar>"
                        "<w:top w:w=\"0\" w:type=\"dxa\"/>"
                        "<w:left w:w=\"58\" w:type=\"dxa\"/>"
                        "<w:bottom w:w=\"0\" w:type=\"dxa\"/>"
                        "<w:right w:w=\"58\" w:type=\"dxa\"/>"
                        "</w:tblCellMar>");
    if (t.hasBorders)
        x += QStringLiteral("<w:tblBorders>"
                            "<w:top w:val=\"single\" w:sz=\"4\" w:color=\"BFBFBF\"/>"
                            "<w:left w:val=\"single\" w:sz=\"4\" w:color=\"BFBFBF\"/>"
                            "<w:bottom w:val=\"single\" w:sz=\"4\" w:color=\"BFBFBF\"/>"
                            "<w:right w:val=\"single\" w:sz=\"4\" w:color=\"BFBFBF\"/>"
                            "<w:insideH w:val=\"single\" w:sz=\"4\" w:color=\"BFBFBF\"/>"
                            "<w:insideV w:val=\"single\" w:sz=\"4\" w:color=\"BFBFBF\"/>"
                            "</w:tblBorders>");
    x += QStringLiteral("</w:tblPr><w:tblGrid>");
    for (double w : t.colWidthsPt)
        x += QStringLiteral("<w:gridCol w:w=\"") + twips(w) + QStringLiteral("\"/>");
    x += QStringLiteral("</w:tblGrid>");

    for (int r = 0; r < t.rowCount; ++r) {
        x += QStringLiteral("<w:tr>");
        if (r < t.rowHeightsPt.size())
            x += QStringLiteral("<w:trPr><w:trHeight w:val=\"")
               + twips(t.rowHeightsPt[r])
               + QStringLiteral("\" w:hRule=\"atLeast\"/></w:trPr>");
        int col = 0;
        while (col < cols) {
            const DocxCell *cell = nullptr;
            for (const DocxCell &c : t.cells)
                if (c.row == r && c.col == col) { cell = &c; break; }

            const int span = cell ? qBound(1, cell->colSpan, cols - col) : 1;
            double width = 0.0;
            for (int s = 0; s < span; ++s) width += t.colWidthsPt[col + s];

            x += QStringLiteral("<w:tc><w:tcPr><w:tcW w:w=\"") + twips(width)
               + QStringLiteral("\" w:type=\"dxa\"/>");
            if (span > 1)
                x += QStringLiteral("<w:gridSpan w:val=\"") + QString::number(span)
                   + QStringLiteral("\"/>");
            if (cell && cell->shading.isValid()
                    && cell->shading != QColor(Qt::white))
                x += QStringLiteral("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"")
                   + colorHex(cell->shading) + QStringLiteral("\"/>");
            x += QStringLiteral("<w:vAlign w:val=\"center\"/></w:tcPr>");

            if (cell && !cell->item.text.trimmed().isEmpty()) {
                DocxBlock cellPara;
                cellPara.kind  = DocxBlock::Kind::Paragraph;
                cellPara.lines = { cell->item };
                cellPara.align = cell->align;
                x += paragraphXml(cellPara, 0.0, true);
            } else {
                x += QStringLiteral("<w:p><w:pPr><w:spacing w:before=\"0\" "
                                    "w:after=\"0\"/></w:pPr></w:p>");
            }
            x += QStringLiteral("</w:tc>");
            col += span;
        }
        x += QStringLiteral("</w:tr>");
    }
    x += QStringLiteral("</w:tbl>");
    return x;
}

// Floating, page-anchored picture. Artwork keeps the exact spot it had in the
// PDF while the text around it stays in the normal flow.
QString pictureXml(const DocxBlock &block, const QString &relId, int id)
{
    const QString name = QStringLiteral("Grafik%1").arg(id);
    return QStringLiteral(
        "<w:r><w:drawing><wp:anchor distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\" "
        "simplePos=\"0\" relativeHeight=\"%1\" behindDoc=\"1\" locked=\"0\" "
        "layoutInCell=\"1\" allowOverlap=\"1\">"
        "<wp:simplePos x=\"0\" y=\"0\"/>"
        "<wp:positionH relativeFrom=\"page\"><wp:posOffset>%2</wp:posOffset></wp:positionH>"
        "<wp:positionV relativeFrom=\"page\"><wp:posOffset>%3</wp:posOffset></wp:positionV>"
        "<wp:extent cx=\"%4\" cy=\"%5\"/>"
        "<wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/><wp:wrapNone/>"
        "<wp:docPr id=\"%6\" name=\"%7\"/><wp:cNvGraphicFramePr/>"
        "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
        "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<pic:nvPicPr><pic:cNvPr id=\"%6\" name=\"%7\"/><pic:cNvPicPr/></pic:nvPicPr>"
        "<pic:blipFill><a:blip r:embed=\"%8\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
        "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%4\" cy=\"%5\"/></a:xfrm>"
        "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
        "</pic:pic></a:graphicData></a:graphic></wp:anchor></w:drawing></w:r>")
        .arg(QString::number(100 + id),
             emu(block.bounds.left()), emu(block.bounds.top()),
             emu(block.bounds.width()), emu(block.bounds.height()),
             QString::number(id), name, relId);
}

// Flat fills and rules do not need a bitmap. VML is intentionally used for
// these simple rectangles: both Word and LibreOffice import it as an editable
// drawing object, and page-relative placement is considerably more reliable
// across their DOCX implementations than a DrawingML canvas.
QString shapeXml(const DocxBlock &block, int id)
{
    const QColor fill = block.fillColor.isValid() ? block.fillColor : QColor(Qt::transparent);
    const bool stroked = block.strokeColor.isValid() && block.strokeWidthPt > 0.0;
    QString x = QStringLiteral("<w:r><w:pict><v:rect id=\"pdfShape%1\" "
                               "style=\"position:absolute;left:0;top:0;"
                               "width:%2pt;height:%3pt;margin-left:%4pt;margin-top:%5pt;"
                               "mso-position-horizontal-relative:page;"
                               "mso-position-vertical-relative:page;"
                               "mso-wrap-style:none;z-index:-251658241\" "
                               "filled=\"%6\" stroked=\"%7\"")
        .arg(id)
        .arg(qMax(0.2, block.bounds.width()), 0, 'f', 2)
        .arg(qMax(0.2, block.bounds.height()), 0, 'f', 2)
        .arg(block.bounds.left(), 0, 'f', 2)
        .arg(block.bounds.top(), 0, 'f', 2)
        .arg(fill.alpha() > 0 ? QStringLiteral("t") : QStringLiteral("f"),
             stroked ? QStringLiteral("t") : QStringLiteral("f"));
    if (fill.alpha() > 0)
        x += QStringLiteral(" fillcolor=\"#") + colorHex(fill) + QStringLiteral("\"");
    if (stroked) {
        x += QStringLiteral(" strokecolor=\"#") + colorHex(block.strokeColor)
           + QStringLiteral("\" strokeweight=\"")
           + QString::number(block.strokeWidthPt, 'f', 2) + QStringLiteral("pt\"");
    }
    x += QStringLiteral(">");
    if (fill.alpha() > 0)
        x += QStringLiteral("<v:fill color=\"#") + colorHex(fill) + QStringLiteral("\"/>");
    if (!stroked)
        x += QStringLiteral("<v:stroke on=\"f\"/>");
    x += QStringLiteral("</v:rect></w:pict></w:r>");
    return x;
}

QString textBoxXml(const DocxBlock &block, int id)
{
    if (block.lines.isEmpty()) return {};
    const ContentItem &item = block.lines.first();
    const double fontPt = item.fontSizePt > 0.0 ? item.fontSizePt : 10.0;
    const double width  = qMax(2.0, block.bounds.width() + qMax(2.0, fontPt * 0.35));
    const double height = qMax(block.bounds.height() + qMax(2.0, fontPt * 0.25),
                               fontPt * 1.22);
    return QStringLiteral(
        "<w:r><w:pict><v:rect id=\"pdfGraphicText%1\" "
        "style=\"position:absolute;left:0;top:0;width:%2pt;height:%3pt;"
        "margin-left:%4pt;margin-top:%5pt;"
        "mso-position-horizontal-relative:page;"
        "mso-position-vertical-relative:page;mso-wrap-style:none;z-index:2\" "
        "filled=\"f\" stroked=\"f\"><v:stroke on=\"f\"/>"
        "<v:textbox inset=\"0,0,0,0\"><w:txbxContent>"
        "<w:p><w:pPr><w:spacing w:before=\"0\" w:after=\"0\"/>"
        "<w:jc w:val=\"%6\"/></w:pPr>%7</w:p>"
        "</w:txbxContent></v:textbox></v:rect></w:pict></w:r>")
        .arg(id)
        .arg(width, 0, 'f', 2)
        .arg(height, 0, 'f', 2)
        .arg(block.bounds.left(), 0, 'f', 2)
        .arg(block.bounds.top(), 0, 'f', 2)
        .arg(alignValue(block.align), textRuns(item));
}

}   // namespace DocxXml
