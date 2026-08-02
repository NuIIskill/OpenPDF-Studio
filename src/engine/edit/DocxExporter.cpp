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
    QByteArray data;        // as handed in — the uncompressed bytes
    uint32_t   crc    { 0 };
    uint32_t   offset { 0 };
    QByteArray stored;      // what actually goes into the archive
    uint16_t   method { 0 };  // 0 = store, 8 = deflate
};

// qCompress emits a zlib stream prefixed with the uncompressed size:
//   [4 bytes size][2 bytes zlib header][deflate data][4 bytes adler32]
// ZIP method 8 wants the bare deflate data, so strip the 6-byte head and the
// 4-byte tail. Falls back to storing whenever that would not be a win.
static void deflateEntry(ZEntry &e)
{
    e.method = 0;
    e.stored = e.data;
    if (e.data.size() < 256) return;
    const QByteArray z = qCompress(e.data, 9);
    if (z.size() <= 10) return;
    const QByteArray raw = z.mid(6, z.size() - 10);
    if (raw.isEmpty() || raw.size() >= e.data.size()) return;
    e.method = 8;
    e.stored = raw;
}

static void writeLocal(QByteArray &zip, ZEntry &e)
{
    e.crc    = crc32Compute(e.data);
    deflateEntry(e);
    e.offset = static_cast<uint32_t>(zip.size());
    zip += "\x50\x4B\x03\x04";
    u16le(zip, 20); u16le(zip, 0); u16le(zip, e.method);
    u16le(zip, 0);  u16le(zip, 0);
    u32le(zip, e.crc);
    u32le(zip, static_cast<uint32_t>(e.stored.size()));
    u32le(zip, static_cast<uint32_t>(e.data.size()));
    u16le(zip, static_cast<uint16_t>(e.name.size()));
    u16le(zip, 0);
    zip += e.name;
    zip += e.stored;
}

static void writeCentral(QByteArray &cd, const ZEntry &e)
{
    cd += "\x50\x4B\x01\x02";
    u16le(cd, 20); u16le(cd, 20); u16le(cd, 0); u16le(cd, e.method);
    u16le(cd, 0);  u16le(cd, 0);
    u32le(cd, e.crc);
    u32le(cd, static_cast<uint32_t>(e.stored.size()));
    u32le(cd, static_cast<uint32_t>(e.data.size()));
    u16le(cd, static_cast<uint16_t>(e.name.size()));
    u16le(cd, 0); u16le(cd, 0); u16le(cd, 0); u16le(cd, 0);
    u32le(cd, 0); u32le(cd, e.offset);
    cd += e.name;
}

// ── DOCX XML builders ─────────────────────────────────────────────────────────

// One PNG inside word/media, already paired with the relationship id the
// document body refers to.
struct MediaPart {
    QString    name;     // file name inside word/media
    QString    relId;
    QByteArray png;
};

// English Metric Units — the unit every DrawingML length is expressed in.
constexpr double kEmuPerPt = 12700.0;

static QString emu(double pt)
{
    return QString::number(qRound64(qMax(0.0, pt) * kEmuPerPt));
}

static QString twips(double pt)
{
    return QString::number(qRound(pt * 20.0));
}

static bool encodePng(const QImage &image, QByteArray *out)
{
    QBuffer buffer(out);
    buffer.open(QIODevice::WriteOnly);
    return !image.isNull() && image.save(&buffer, "PNG");
}

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

static double fontSizeOf_(const ContentItem &item)
{
    return item.fontSizePt > 0.0 ? item.fontSizePt
                                 : qMax(6.0, item.bounds.height() * 0.9);
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

// ── structured export ─────────────────────────────────────────────────────────
// Real WordprocessingML: flowing paragraphs, real tables with cell shading, and
// pictures only for content a word processor cannot express. This is what makes
// the result editable rather than a picture of a document.

// Joins the lines of one paragraph back into flowing text. A hyphen at a line
// end followed by a lower-case letter is a hyphenation break and disappears;
// before an upper-case letter it is a real compound hyphen ("Hardware-
// Lifecycle") and must survive.
static QString paragraphText(const QList<ContentItem> &lines)
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

static QString runProperties(const ContentItem &style)
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

static QString alignValue(Qt::Alignment align)
{
    if (align & Qt::AlignHCenter) return QStringLiteral("center");
    if (align & Qt::AlignRight)   return QStringLiteral("right");
    return QStringLiteral("left");
}

// Line height is pinned to the pitch measured in the PDF. Left on "auto", Word
// applies the substituted font's own leading and every block drifts a little
// further down the page than the original.
static int linePitchTwips(const DocxBlock &block)
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

static QString paragraphXml(const DocxBlock &block, double spaceBeforePt,
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

static QString tableXml(const DocxBlock &block)
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
static QString pictureXml(const DocxBlock &block, const QString &relId, int id)
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

static QByteArray buildStructuredDocument(const QList<DocxPage> &pages,
                                          QList<MediaPart> *media)
{
    QString x = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document "
        "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
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
    for (int pg = 0; pg < pages.size(); ++pg) {
        const DocxPage &page = pages[pg];
        const double indentShift = page.marginsPt.left() - section.left();

        // Every picture of this page is anchored from one leading paragraph.
        // Its own height must be negligible or it would push the flow down.
        QString anchors;
        for (const DocxBlock &block : page.blocks) {
            if (block.kind != DocxBlock::Kind::Picture) continue;
            QByteArray png;
            if (!encodePng(block.picture, &png)) continue;
            MediaPart part;
            part.name  = QStringLiteral("image%1.png").arg(mediaId);
            part.relId = QStringLiteral("rIdImg%1").arg(mediaId);
            part.png   = std::move(png);
            anchors += pictureXml(block, part.relId, mediaId);
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
            if (block.kind == DocxBlock::Kind::Picture) continue;
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
                                          QList<MediaPart> *media)
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
        if (!pages[pg].background.isNull()
                && encodePng(pages[pg].background, &backgroundPng)) {
            const QSizeF ps = pages[pg].pageSizePt.isEmpty()
                                  ? QSizeF(595.0, 842.0) : pages[pg].pageSizePt;
            media->append({ QStringLiteral("page%1.png").arg(pg + 1),
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

    QList<ZEntry> entries;
    entries.append({"[Content_Types].xml",          QByteArray(kCT),      0, 0});
    entries.append({"_rels/.rels",                  QByteArray(kRels),    0, 0});
    entries.append({"word/_rels/document.xml.rels", docRels, 0, 0});
    entries.append({"word/styles.xml",              QByteArray(kStyles),  0, 0});
    entries.append({"word/document.xml",            documentXml, 0, 0});
    for (const MediaPart &part : media)
        entries.append({("word/media/" + part.name).toUtf8(), part.png, 0, 0});

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
    QList<MediaPart> media;

    // Structured output is the goal: real paragraphs, real tables, pictures
    // only where nothing else fits. Pages the layout pass could not read —
    // scans, pure artwork — keep the positioned raster fallback.
    bool structured = !pages.isEmpty();
    for (const DocxPage &page : pages)
        if (page.blocks.isEmpty()) structured = false;
    if (structured)
        return writeDocx(outputPath, buildStructuredDocument(pages, &media), media);

    if (prefersSemanticLayout(pages))
        return writeDocx(outputPath, buildSemanticDocument(pages), {});
    return writeDocx(outputPath, buildPositionedDocument(pages, &media), media);
}

bool DocxExporter::exportToDocx(const QString &outputPath,
                                const QList<QString> &pageTexts,
                                const QString &title)
{
    return writeDocx(outputPath, buildDocument(pageTexts, title), {});
}
