#include "engine/import/OdtReader.hpp"

#include "engine/import/DocumentBuilder.hpp"
#include "engine/import/OdfStyles.hpp"
#include "engine/import/XmlRead.hpp"
#include "engine/import/ZipArchive.hpp"

#include <QCoreApplication>
#include <QImage>
#include <QXmlStreamReader>

using XmlRead::attribute;
using XmlRead::odfLengthToPt;

namespace {

constexpr int MaxTableDepth = 5;
constexpr int MaxListDepth  = 9;

QString tr(const char *text) { return QCoreApplication::translate("DocumentImport", text); }

struct Context {
    const ZipArchive &zip;
    const OdfStyles  &styles;
    DocumentBuilder  &builder;
    int tableDepth { 0 };
};

void readParagraph(QXmlStreamReader &xml, Context &context,
                   const std::optional<ListStyle> &list);
void readTable(QXmlStreamReader &xml, Context &context);

/// A frame holds the size; the picture itself is either a file in the archive
/// or, in a document saved as one flat file, base64 right inside it.
void readFrame(QXmlStreamReader &xml, Context &context)
{
    const QSizeF size(odfLengthToPt(attribute(xml, u"width")),
                      odfLengthToPt(attribute(xml, u"height")));
    QString    href;
    QByteArray inlineData;

    for (int depth = 1; depth > 0 && !xml.atEnd();) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == u"binary-data") {
                inlineData = QByteArray::fromBase64(xml.readElementText().toUtf8());
                continue;   // the reader already sits on the closing tag
            }
            if (xml.name() == u"image" && href.isEmpty())
                href = attribute(xml, u"href").toString();
            ++depth;
        } else if (token == QXmlStreamReader::EndElement) {
            --depth;
        }
    }

    QByteArray data = inlineData;
    if (data.isEmpty() && !href.isEmpty()) {
        while (href.startsWith(QLatin1String("./"))) href = href.mid(2);
        data = context.zip.read(href);
    }

    QImage image;
    if (image.loadFromData(data)) context.builder.appendImage(image, size);
}

/// Everything inside one paragraph, spans and all.
void readInline(QXmlStreamReader &xml, Context &context, const TextStyle &style)
{
    while (!xml.atEnd()) {
        const auto token = xml.readNext();

        if (token == QXmlStreamReader::Characters) {
            context.builder.appendText(xml.text().toString(), style);
        } else if (token == QXmlStreamReader::StartElement) {
            const QStringView name = xml.name();

            if (name == u"span") {
                TextStyle inner = style;
                inner.merge(context.styles.textFor(attribute(xml, u"style-name").toString()));
                readInline(xml, context, inner);
            } else if (name == u"a" || name == u"bookmark-ref" || name == u"ruby-base") {
                readInline(xml, context, style);
            } else if (name == u"s") {
                const int count = qMax(1, attribute(xml, u"c").toInt());
                context.builder.appendText(QString(count, QLatin1Char(' ')), style);
                xml.skipCurrentElement();
            } else if (name == u"tab") {
                context.builder.appendText(QStringLiteral("\t"), style);
                xml.skipCurrentElement();
            } else if (name == u"line-break") {
                context.builder.appendLineBreak(style);
                xml.skipCurrentElement();
            } else if (name == u"frame") {
                readFrame(xml, context);
            } else {
                xml.skipCurrentElement();
            }
        } else if (token == QXmlStreamReader::EndElement) {
            return;
        }
    }
}

void readParagraph(QXmlStreamReader &xml, Context &context,
                   const std::optional<ListStyle> &list)
{
    const QString styleName = attribute(xml, u"style-name").toString();
    const bool    isHeading = xml.name() == u"h";

    BlockStyle block = context.styles.blockFor(styleName);
    if (isHeading) {
        const int level = attribute(xml, u"outline-level").toInt();
        if (level >= 1 && level <= 6) block.headingLevel = level;
    }

    context.builder.startParagraph(block, list);
    readInline(xml, context, context.styles.textFor(styleName));
}

void readList(QXmlStreamReader &xml, Context &context, const QString &styleName,
              int level)
{
    QString style = attribute(xml, u"style-name").toString();
    if (style.isEmpty()) style = styleName;

    while (xml.readNextStartElement()) {
        if (xml.name() != u"list-item" && xml.name() != u"list-header") {
            xml.skipCurrentElement();
            continue;
        }
        while (xml.readNextStartElement()) {
            const QStringView name = xml.name();
            if (name == u"p" || name == u"h") {
                readParagraph(xml, context,
                              ListStyle { context.styles.isNumbered(style, level), level });
            } else if (name == u"list" && level + 1 < MaxListDepth) {
                readList(xml, context, style, level + 1);
            } else {
                xml.skipCurrentElement();
            }
        }
    }
}

void readCell(QXmlStreamReader &xml, Context &context)
{
    context.builder.startTableCell(
        qMax(1, attribute(xml, u"number-columns-spanned").toInt()),
        context.styles.cellFor(attribute(xml, u"style-name").toString()));

    while (xml.readNextStartElement()) {
        const QStringView name = xml.name();
        if      (name == u"p" || name == u"h") readParagraph(xml, context, {});
        else if (name == u"list")              readList(xml, context, QString(), 0);
        else if (name == u"table")             readTable(xml, context);
        else                                   xml.skipCurrentElement();
    }
}

void readRows(QXmlStreamReader &xml, Context &context)
{
    while (xml.readNextStartElement()) {
        const QStringView name = xml.name();
        if (name == u"table-cell") {
            readCell(xml, context);
        } else if (name == u"covered-table-cell") {
            // The cell a span reaches over; it has no content of its own.
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
}

void readTable(QXmlStreamReader &xml, Context &context)
{
    if (context.tableDepth >= MaxTableDepth) { xml.skipCurrentElement(); return; }

    QList<double> columns;
    bool          started = false;

    auto startRow = [&] {
        if (!started) {
            if (columns.isEmpty()) columns.append(0.0);
            context.builder.startTable(columns);
            ++context.tableDepth;
            started = true;
        }
        context.builder.startTableRow();
    };

    while (xml.readNextStartElement()) {
        const QStringView name = xml.name();

        if (name == u"table-column") {
            const double width = context.styles.columnWidthPt(
                attribute(xml, u"style-name").toString());
            const int repeat = qBound(1, attribute(xml, u"number-columns-repeated").toInt(), 64);
            for (int i = 0; i < repeat; ++i) columns.append(width);
            xml.skipCurrentElement();
            continue;
        }
        if (name == u"table-row") {
            startRow();
            readRows(xml, context);
            continue;
        }
        if (name == u"table-header-rows" || name == u"table-row-group") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"table-row") { startRow(); readRows(xml, context); }
                else                            xml.skipCurrentElement();
            }
            continue;
        }
        xml.skipCurrentElement();
    }

    if (started) {
        --context.tableDepth;
        context.builder.endTable();
    }
}

void readBody(QXmlStreamReader &xml, Context &context)
{
    while (xml.readNextStartElement()) {
        const QStringView name = xml.name();
        if      (name == u"p" || name == u"h") readParagraph(xml, context, {});
        else if (name == u"list")              readList(xml, context, QString(), 0);
        else if (name == u"table")             readTable(xml, context);
        else if (name == u"section")           readBody(xml, context);
        else                                   xml.skipCurrentElement();
    }
}

}

bool OdtReader::read(const ZipArchive &zip, DocumentBuilder &builder, QString *error)
{
    const QByteArray data = zip.read(QStringLiteral("content.xml"));
    if (data.isEmpty()) {
        if (error) *error = tr("The file has no readable document part.");
        return false;
    }

    OdfStyles styles;
    styles.load(zip, data);
    builder.setPageSizePt(styles.pageSizePt());
    builder.setMarginsPt(styles.marginsPt());
    builder.setDefaultFont(styles.defaultFamily(), styles.defaultSizePt());

    Context context { zip, styles, builder, 0 };
    QXmlStreamReader xml(data);

    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) continue;
        if (xml.name() != u"text") continue;
        readBody(xml, context);
        break;
    }

    if (xml.hasError()) {
        if (error) *error = tr("The document is damaged: %1").arg(xml.errorString());
        return false;
    }
    return true;
}
