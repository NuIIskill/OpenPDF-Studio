#include "engine/import/DocxReader.hpp"

#include "engine/import/DocumentBuilder.hpp"
#include "engine/import/DocxStyles.hpp"
#include "engine/import/XmlRead.hpp"
#include "engine/import/ZipArchive.hpp"

#include <QCoreApplication>
#include <QImage>
#include <QHash>
#include <QMarginsF>
#include <optional>
#include <QXmlStreamReader>

using XmlRead::attribute;

namespace {

constexpr int MaxTableDepth = 5;

QString tr(const char *text) { return QCoreApplication::translate("DocumentImport", text); }

struct Context {
    const ZipArchive &zip;
    const DocxStyles &styles;
    DocumentBuilder  &builder;
    int tableDepth { 0 };
};

void readParagraph(QXmlStreamReader &xml, Context &context);
void readTable(QXmlStreamReader &xml, Context &context);

/// w:tblCellMar and w:tcMar, the inset between a cell's border and its text.
std::optional<QMarginsF> readCellMargins(QXmlStreamReader &xml)
{
    QMarginsF margins;
    while (xml.readNextStartElement()) {
        const double value = XmlRead::twipsToPt(attribute(xml, u"w"));
        if      (xml.name() == u"top")    margins.setTop(value);
        else if (xml.name() == u"bottom") margins.setBottom(value);
        else if (xml.name() == u"left" || xml.name() == u"start")  margins.setLeft(value);
        else if (xml.name() == u"right" || xml.name() == u"end")   margins.setRight(value);
        xml.skipCurrentElement();
    }
    return margins;
}

/// w:tblBorders and w:tcBorders name every side on its own. One width and one
/// colour for the whole thing is as much as the layout can show anyway, so the
/// strongest side wins.
void readBorders(QXmlStreamReader &xml, std::optional<double> *width,
                 std::optional<QColor> *color)
{
    double widest = 0.0;
    QColor found;
    bool   stated = false;

    while (xml.readNextStartElement()) {
        const QStringView side = xml.name();
        if (side == u"top" || side == u"left" || side == u"bottom"
                || side == u"right" || side == u"insideH" || side == u"insideV") {
            stated = true;
            const QStringView kind = attribute(xml, u"val");
            if (kind != QStringView(u"none") && kind != QStringView(u"nil")) {
                widest = qMax(widest, XmlRead::eighthPointsToPt(attribute(xml, u"sz")));
                const QColor sideColor = XmlRead::hexColor(attribute(xml, u"color"));
                if (sideColor.isValid() && !found.isValid()) found = sideColor;
            }
        }
        xml.skipCurrentElement();
    }

    if (!stated) return;
    *width = widest;
    if (found.isValid()) *color = found;
}

/// The geometry of a legacy VML shape sits in one CSS-like style attribute.
QHash<QStringView, QStringView> vmlStyle(QStringView style)
{
    QHash<QStringView, QStringView> values;
    for (const QStringView part : style.tokenize(u';', Qt::SkipEmptyParts)) {
        const qsizetype colon = part.indexOf(u':');
        if (colon <= 0) continue;
        values.insert(part.left(colon).trimmed(), part.mid(colon + 1).trimmed());
    }
    return values;
}

/// A drawing carries its size and its relationship id somewhere below itself;
/// which way round depends on whether it is inline, anchored or a legacy VML
/// shape, so the whole subtree is scanned for the handful of elements that say
/// something.
void readDrawing(QXmlStreamReader &xml, Context &context)
{
    QSizeF  size;
    QPointF pagePosition;
    QString relationshipId;
    QColor  fill;
    QColor  stroke;
    bool    anchored   = false;
    bool    behindText = false;
    bool    horizontal = true;

    // A VML shape can carry a text box, and a document converted out of a PDF
    // is made almost entirely of those. Only the text and the first run's
    // formatting are taken; that is what such a box holds.
    QString       boxText;
    TextStyle     boxStyle;
    Qt::Alignment boxAlign = Qt::AlignLeft | Qt::AlignTop;
    bool          inTextBox = false;

    for (int depth = 1; depth > 0 && !xml.atEnd();) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::EndElement) { --depth; continue; }
        if (token != QXmlStreamReader::StartElement) continue;

        const QStringView name = xml.name();

        if (name == u"t" && inTextBox) {
            boxText += xml.readElementText();
            continue;
        }
        if (name == u"posOffset") {
            const double offset = XmlRead::emuToPt(xml.readElementText());
            if (horizontal) pagePosition.setX(offset);
            else            pagePosition.setY(offset);
            continue;               // the reader already sits on the closing tag
        }
        ++depth;

        if (name == u"textbox") {
            inTextBox = true;
        } else if (name == u"br" && inTextBox) {
            boxText += QLatin1Char('\n');
        } else if (name == u"jc" && inTextBox) {
            const QStringView value = attribute(xml, u"val");
            if (value == QStringView(u"center")) boxAlign = Qt::AlignHCenter | Qt::AlignTop;
            else if (value == QStringView(u"right") || value == QStringView(u"end"))
                boxAlign = Qt::AlignRight | Qt::AlignTop;
        } else if (name == u"rPr" && inTextBox && !boxStyle.sizePt) {
            boxStyle = DocxStyles::readRunProperties(xml);
            --depth;                 // readRunProperties consumed the element
        } else if (name == u"anchor") {
            anchored   = true;
            behindText = attribute(xml, u"behindDoc") == QStringView(u"1");
        } else if (name == u"positionH") {
            horizontal = true;
        } else if (name == u"positionV") {
            horizontal = false;
        } else if (name == u"extent") {
            size = QSizeF(XmlRead::emuToPt(attribute(xml, u"cx")),
                          XmlRead::emuToPt(attribute(xml, u"cy")));
        } else if (name == u"blip") {
            relationshipId = attribute(xml, u"embed").toString();
        } else if (name == u"imagedata") {
            relationshipId = attribute(xml, u"id").toString();
        } else if (name == u"rect" || name == u"roundrect" || name == u"oval"
                   || name == u"shape" || name == u"background") {
            const auto style = vmlStyle(attribute(xml, u"style"));
            const QSizeF shapeSize(XmlRead::odfLengthToPt(style.value(u"width")),
                                   XmlRead::odfLengthToPt(style.value(u"height")));
            if (!shapeSize.isEmpty()) {
                size = shapeSize;
                pagePosition = QPointF(XmlRead::odfLengthToPt(style.value(u"margin-left")),
                                       XmlRead::odfLengthToPt(style.value(u"margin-top")));
            }
            // A negative stacking order is Word's way of saying "behind the text".
            if (style.value(u"z-index").toDouble() < 0.0) {
                anchored   = true;
                behindText = true;
            }
            if (attribute(xml, u"filled") != QStringView(u"f"))
                fill = XmlRead::hexColor(attribute(xml, u"fillcolor"));
            if (attribute(xml, u"stroked") != QStringView(u"f"))
                stroke = XmlRead::hexColor(attribute(xml, u"strokecolor"));
        }
    }

    QImage image;
    if (!relationshipId.isEmpty()) {
        QString target = context.styles.relationshipTarget(relationshipId);
        while (target.startsWith(QLatin1String("../"))) target = target.mid(3);
        if (target.startsWith(QLatin1Char('/'))) target = target.mid(1);
        else if (!target.isEmpty())              target.prepend(QStringLiteral("word/"));
        if (!target.isEmpty()) image.loadFromData(context.zip.read(target));
    }

    // Anything that sits behind the text is placed on the page, not in the flow:
    // putting it in the flow would push the real content around, and leaving it
    // out drops the panels and bands a designed document is made of.
    if (anchored || !boxText.isEmpty()) {
        DocumentBuilder::Decoration decoration;
        decoration.pageRectPt = QRectF(pagePosition, size);
        decoration.fill       = fill;
        decoration.stroke     = stroke;
        decoration.text       = boxText;
        decoration.textStyle  = boxStyle;
        decoration.textAlign  = boxAlign;
        if (behindText) decoration.image = image;
        context.builder.addDecoration(decoration);

        // A wide band flush with the top edge is a page header: the paragraph
        // it hangs on is the one that opens that page.
        if (behindText && fill.isValid() && pagePosition.y() <= 1.0
                && size.width() >= 200.0 && size.height() >= 4.0)
            context.builder.markPageStart();
        if (!behindText && !image.isNull()) context.builder.appendImage(image, size);
        return;
    }

    if (!image.isNull()) context.builder.appendImage(image, size);
}

void readRun(QXmlStreamReader &xml, Context &context, const TextStyle &paragraphStyle)
{
    TextStyle style = paragraphStyle;

    while (xml.readNextStartElement()) {
        const QStringView name = xml.name();

        if (name == u"rPr") {
            style.merge(DocxStyles::readRunProperties(xml));
        } else if (name == u"t") {
            context.builder.appendText(xml.readElementText(), style);
        } else if (name == u"tab") {
            context.builder.appendText(QStringLiteral("\t"), style);
            xml.skipCurrentElement();
        } else if (name == u"br") {
            if (attribute(xml, u"type") == QStringView(u"page"))
                context.builder.appendPageBreak();
            else
                context.builder.appendLineBreak(style);
            xml.skipCurrentElement();
        } else if (name == u"drawing" || name == u"pict" || name == u"object") {
            readDrawing(xml, context);
        } else {
            xml.skipCurrentElement();
        }
    }
}

void readParagraph(QXmlStreamReader &xml, Context &context)
{
    BlockStyle  block     = context.styles.defaultBlock();
    TextStyle   textStyle = context.styles.defaultText();
    DocxListRef list;
    bool        started   = false;

    auto start = [&] {
        if (started) return;
        started = true;
        std::optional<ListStyle> listStyle;
        if (list.numberId > 0)
            listStyle = ListStyle { context.styles.isNumbered(list), qBound(0, list.level, 8) };
        context.builder.startParagraph(block, listStyle);
    };

    while (xml.readNextStartElement()) {
        const QStringView name = xml.name();

        if (name == u"pPr" && !started) {
            QString styleId;
            bool    pageBreakBefore = false;
            const BlockStyle direct = DocxStyles::readParagraphProperties(
                xml, &styleId, &list, &pageBreakBefore);

            // A style can put the paragraph in a list all by itself; ListBullet
            // and ListParagraph do exactly that and carry no w:numPr of their own.
            if (list.numberId <= 0) list = context.styles.listFor(styleId);

            block     = context.styles.blockFor(styleId);
            textStyle = context.styles.textFor(styleId);
            block.merge(direct);
            if (pageBreakBefore) context.builder.appendPageBreak();
            continue;
        }

        start();

        if (name == u"r") {
            readRun(xml, context, textStyle);
        } else if (name == u"hyperlink" || name == u"ins" || name == u"smartTag"
                   || name == u"sdt" || name == u"sdtContent" || name == u"bdo") {
            // Wrappers that hold ordinary runs. A deleted range (w:del) is the
            // one that is skipped instead, because its text is not in the
            // document any more.
            while (xml.readNextStartElement()) {
                if (xml.name() == u"r") readRun(xml, context, textStyle);
                else if (xml.name() == u"del") xml.skipCurrentElement();
                else xml.skipCurrentElement();
            }
        } else {
            xml.skipCurrentElement();
        }
    }
    start();
}

void readCell(QXmlStreamReader &xml, Context &context, const CellStyle &tableDefault)
{
    int       span    = 1;
    bool      started = false;
    CellStyle style   = tableDefault;

    auto start = [&] {
        if (started) return;
        started = true;
        context.builder.startTableCell(span, style);
    };

    while (xml.readNextStartElement()) {
        if (xml.name() == u"tcPr" && !started) {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"gridSpan") {
                    span = qMax(1, attribute(xml, u"val").toInt());
                } else if (xml.name() == u"tcBorders") {
                    readBorders(xml, &style.borderWidthPt, &style.borderColor);
                    continue;
                } else if (xml.name() == u"tcMar") {
                    style.paddingPt = readCellMargins(xml);
                    continue;
                } else if (xml.name() == u"shd") {
                    const QColor fill = XmlRead::hexColor(attribute(xml, u"fill"));
                    if (fill.isValid()) style.background = fill;
                }
                xml.skipCurrentElement();
            }
            continue;
        }
        start();

        if (xml.name() == u"p")        readParagraph(xml, context);
        else if (xml.name() == u"tbl") readTable(xml, context);
        else                           xml.skipCurrentElement();
    }
    start();
}

void readTable(QXmlStreamReader &xml, Context &context)
{
    if (context.tableDepth >= MaxTableDepth) { xml.skipCurrentElement(); return; }

    QList<double> columns;
    bool          started = false;
    TableStyle    table;
    CellStyle     cellDefault;

    while (xml.readNextStartElement()) {
        const QStringView name = xml.name();

        if (name == u"tblPr") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"tblBorders") {
                    readBorders(xml, &table.borderWidthPt, &table.borderColor);
                    cellDefault.borderWidthPt = table.borderWidthPt;
                    cellDefault.borderColor   = table.borderColor;
                    continue;
                }
                if (xml.name() == u"tblCellMar") {
                    cellDefault.paddingPt = readCellMargins(xml);
                    continue;
                }
                xml.skipCurrentElement();
            }
            continue;
        }
        if (name == u"tblGrid") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"gridCol")
                    columns.append(XmlRead::twipsToPt(attribute(xml, u"w")));
                xml.skipCurrentElement();
            }
            continue;
        }
        if (name == u"tr") {
            if (!started) {
                if (columns.isEmpty()) columns.append(0.0);
                context.builder.startTable(columns, table);
                ++context.tableDepth;
                started = true;
            }

            bool rowStarted = false;
            while (xml.readNextStartElement()) {
                if (xml.name() == u"trPr" && !rowStarted) {
                    double minimum = 0.0;
                    while (xml.readNextStartElement()) {
                        if (xml.name() == u"trHeight"
                                && attribute(xml, u"hRule") != QStringView(u"exact"))
                            minimum = XmlRead::twipsToPt(attribute(xml, u"val"));
                        xml.skipCurrentElement();
                    }
                    context.builder.startTableRow(minimum);
                    rowStarted = true;
                    continue;
                }
                if (!rowStarted) { context.builder.startTableRow(); rowStarted = true; }

                if (xml.name() == u"tc") readCell(xml, context, cellDefault);
                else                     xml.skipCurrentElement();
            }
            if (!rowStarted) context.builder.startTableRow();
            continue;
        }
        xml.skipCurrentElement();
    }

    if (started) {
        --context.tableDepth;
        context.builder.endTable();
    }
}

void readSectionProperties(QXmlStreamReader &xml, DocumentBuilder &builder)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == u"pgSz") {
            QSizeF size(XmlRead::twipsToPt(attribute(xml, u"w")),
                        XmlRead::twipsToPt(attribute(xml, u"h")));
            if (attribute(xml, u"orient") == QStringView(u"landscape")
                    && size.width() < size.height())
                size.transpose();
            builder.setPageSizePt(size);
        } else if (xml.name() == u"pgMar") {
            builder.setMarginsPt(QMarginsF(XmlRead::twipsToPt(attribute(xml, u"left")),
                                           XmlRead::twipsToPt(attribute(xml, u"top")),
                                           XmlRead::twipsToPt(attribute(xml, u"right")),
                                           XmlRead::twipsToPt(attribute(xml, u"bottom"))));
        }
        xml.skipCurrentElement();
    }
}

}

bool DocxReader::read(const ZipArchive &zip, DocumentBuilder &builder, QString *error)
{
    const QByteArray data = zip.read(QStringLiteral("word/document.xml"));
    if (data.isEmpty()) {
        if (error) *error = tr("The file has no readable document part.");
        return false;
    }

    DocxStyles styles;
    styles.load(zip);
    builder.setDefaultFont(styles.defaultText().family.value_or(QString()),
                           styles.defaultText().sizePt.value_or(0.0));

    Context context { zip, styles, builder, 0 };
    QXmlStreamReader xml(data);

    while (xml.readNextStartElement()) {
        if (xml.name() != u"document") { xml.skipCurrentElement(); continue; }

        while (xml.readNextStartElement()) {
            if (xml.name() != u"body") { xml.skipCurrentElement(); continue; }

            while (xml.readNextStartElement()) {
                const QStringView name = xml.name();
                if      (name == u"p")      readParagraph(xml, context);
                else if (name == u"tbl")    readTable(xml, context);
                else if (name == u"sectPr") readSectionProperties(xml, builder);
                else                        xml.skipCurrentElement();
            }
        }
    }

    if (xml.hasError()) {
        if (error) *error = tr("The document is damaged: %1").arg(xml.errorString());
        return false;
    }
    return true;
}
