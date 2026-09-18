#include "engine/import/DocxStyles.hpp"

#include "engine/import/XmlRead.hpp"
#include "engine/import/ZipArchive.hpp"

#include <QTextCharFormat>
#include <QXmlStreamReader>

using XmlRead::attribute;

namespace {

constexpr int MaxStyleDepth = 12;

/// "heading 3" and "Heading3" both mean the same thing, and files in the wild
/// carry one or the other.
std::optional<int> headingLevelOf(const QString &styleId, const QString &name)
{
    for (const QString &candidate : { name.toLower(), styleId.toLower() }) {
        if (!candidate.startsWith(QLatin1String("heading"))) continue;
        const QString digits = candidate.mid(7).trimmed();
        bool ok = false;
        const int level = digits.toInt(&ok);
        if (ok && level >= 1 && level <= 6) return level;
    }
    return {};
}

Qt::Alignment alignmentOf(QStringView value)
{
    if (value == QStringView(u"center"))  return Qt::AlignHCenter;
    if (value == QStringView(u"right") || value == QStringView(u"end"))
        return Qt::AlignRight;
    if (value == QStringView(u"both") || value == QStringView(u"distribute"))
        return Qt::AlignJustify;
    return Qt::AlignLeft;
}

}

TextStyle DocxStyles::readRunProperties(QXmlStreamReader &xml)
{
    TextStyle style;
    while (xml.readNextStartElement()) {
        const QStringView name  = xml.name();
        const QStringView value = attribute(xml, u"val");

        if      (name == u"b")      style.bold      = XmlRead::onOff(value);
        else if (name == u"i")      style.italic    = XmlRead::onOff(value);
        else if (name == u"u")      style.underline = XmlRead::onOff(value);
        else if (name == u"strike") style.strikeOut = XmlRead::onOff(value);
        else if (name == u"sz") {
            const double halfPoints = value.toDouble();
            if (halfPoints > 0.0) style.sizePt = halfPoints / 2.0;
        }
        else if (name == u"color") {
            const QColor color = XmlRead::hexColor(value);
            if (color.isValid()) style.color = color;
        }
        else if (name == u"rFonts") {
            const QStringView family = attribute(xml, u"ascii");
            if (!family.isEmpty()) style.family = family.toString();
        }
        else if (name == u"vertAlign") {
            if (value == QStringView(u"superscript"))
                style.verticalAlign = QTextCharFormat::AlignSuperScript;
            else if (value == QStringView(u"subscript"))
                style.verticalAlign = QTextCharFormat::AlignSubScript;
        }
        xml.skipCurrentElement();
    }
    return style;
}

BlockStyle DocxStyles::readParagraphProperties(QXmlStreamReader &xml,
                                               QString *styleId,
                                               DocxListRef *list,
                                               bool *pageBreakBefore)
{
    BlockStyle block;
    while (xml.readNextStartElement()) {
        const QStringView name  = xml.name();
        const QStringView value = attribute(xml, u"val");

        if (name == u"pStyle") {
            if (styleId) *styleId = value.toString();
        }
        else if (name == u"jc") block.alignment = alignmentOf(value);
        else if (name == u"pageBreakBefore") {
            if (pageBreakBefore) *pageBreakBefore = XmlRead::onOff(value);
        }
        else if (name == u"ind") {
            for (const QStringView side : { QStringView(u"left"), QStringView(u"start") })
                if (XmlRead::hasAttribute(xml, side))
                    block.leftIndentPt = XmlRead::twipsToPt(attribute(xml, side));
            for (const QStringView side : { QStringView(u"right"), QStringView(u"end") })
                if (XmlRead::hasAttribute(xml, side))
                    block.rightIndentPt = XmlRead::twipsToPt(attribute(xml, side));
            if (XmlRead::hasAttribute(xml, u"firstLine"))
                block.firstLineIndentPt = XmlRead::twipsToPt(attribute(xml, u"firstLine"));
            if (XmlRead::hasAttribute(xml, u"hanging"))
                block.firstLineIndentPt = -XmlRead::twipsToPt(attribute(xml, u"hanging"));
        }
        else if (name == u"spacing") {
            if (XmlRead::hasAttribute(xml, u"before"))
                block.spaceBeforePt = XmlRead::twipsToPt(attribute(xml, u"before"));
            if (XmlRead::hasAttribute(xml, u"after"))
                block.spaceAfterPt = XmlRead::twipsToPt(attribute(xml, u"after"));
            // With the default rule the line height counts in 240ths of a line.
            // "exact" and "atLeast" measure in twips instead and pin the line,
            // which is what a document converted out of a PDF relies on: ignore
            // it and every paragraph drifts down the page.
            const QStringView rule = attribute(xml, u"lineRule");
            const double line = attribute(xml, u"line").toDouble();
            if (line > 0.0) {
                if (rule == QStringView(u"exact"))
                    block.lineHeightExactPt = line / 20.0;
                else if (rule == QStringView(u"atLeast"))
                    block.lineHeightMinimumPt = line / 20.0;
                else
                    block.lineHeightPercent = line / 240.0 * 100.0;
            }
        }
        else if (name == u"shd") {
            const QColor fill = XmlRead::hexColor(attribute(xml, u"fill"));
            if (fill.isValid()) block.background = fill;
        }
        else if (name == u"numPr") {
            DocxListRef found;
            while (xml.readNextStartElement()) {
                if (xml.name() == u"ilvl")
                    found.level = attribute(xml, u"val").toInt();
                else if (xml.name() == u"numId")
                    found.numberId = attribute(xml, u"val").toInt();
                xml.skipCurrentElement();
            }
            if (list) *list = found;
            continue;
        }
        xml.skipCurrentElement();
    }
    return block;
}

void DocxStyles::load(const ZipArchive &zip)
{
    readStyles(zip.read(QStringLiteral("word/styles.xml")));
    readNumbering(zip.read(QStringLiteral("word/numbering.xml")));
    readRelationships(zip.read(QStringLiteral("word/_rels/document.xml.rels")));
}

void DocxStyles::readStyles(const QByteArray &data)
{
    if (data.isEmpty()) return;
    QXmlStreamReader xml(data);

    while (xml.readNextStartElement()) {
        if (xml.name() != u"styles") { xml.skipCurrentElement(); continue; }

        while (xml.readNextStartElement()) {
            if (xml.name() == u"docDefaults") {
                while (xml.readNextStartElement()) {
                    const bool run = xml.name() == u"rPrDefault";
                    while (xml.readNextStartElement()) {
                        if (run) m_defaultText = readRunProperties(xml);
                        else     m_defaultBlock = readParagraphProperties(xml);
                    }
                }
                continue;
            }
            if (xml.name() != u"style") { xml.skipCurrentElement(); continue; }

            const QString id = attribute(xml, u"styleId").toString();
            const bool isDefault = attribute(xml, u"type") == QStringView(u"paragraph")
                                && XmlRead::onOff(attribute(xml, u"default"))
                                && !attribute(xml, u"default").isNull();
            Style style;
            while (xml.readNextStartElement()) {
                const QStringView name = xml.name();
                if (name == u"name") {
                    style.name = attribute(xml, u"val").toString();
                    xml.skipCurrentElement();
                } else if (name == u"basedOn") {
                    style.basedOn = attribute(xml, u"val").toString();
                    xml.skipCurrentElement();
                } else if (name == u"rPr") {
                    style.text = readRunProperties(xml);
                } else if (name == u"pPr") {
                    style.block = readParagraphProperties(xml, nullptr, &style.list);
                } else {
                    xml.skipCurrentElement();
                }
            }
            if (const auto level = headingLevelOf(id, style.name))
                style.block.headingLevel = level;
            if (!id.isEmpty()) m_styles.insert(id, style);
            if (isDefault && m_defaultStyleId.isEmpty()) m_defaultStyleId = id;
        }
    }
}

void DocxStyles::readNumbering(const QByteArray &data)
{
    if (data.isEmpty()) return;
    QXmlStreamReader xml(data);

    while (xml.readNextStartElement()) {
        if (xml.name() != u"numbering") { xml.skipCurrentElement(); continue; }

        while (xml.readNextStartElement()) {
            if (xml.name() == u"abstractNum") {
                const int id = attribute(xml, u"abstractNumId").toInt();
                bool numbered = false;
                while (xml.readNextStartElement()) {
                    if (xml.name() == u"lvl" && attribute(xml, u"ilvl").toInt() == 0) {
                        while (xml.readNextStartElement()) {
                            if (xml.name() == u"numFmt")
                                numbered = attribute(xml, u"val") != QStringView(u"bullet");
                            xml.skipCurrentElement();
                        }
                        continue;
                    }
                    xml.skipCurrentElement();
                }
                m_abstractIsNumbered.insert(id, numbered);
                continue;
            }
            if (xml.name() == u"num") {
                const int id = attribute(xml, u"numId").toInt();
                while (xml.readNextStartElement()) {
                    if (xml.name() == u"abstractNumId")
                        m_numberToAbstract.insert(id, attribute(xml, u"val").toInt());
                    xml.skipCurrentElement();
                }
                continue;
            }
            xml.skipCurrentElement();
        }
    }
}

void DocxStyles::readRelationships(const QByteArray &data)
{
    if (data.isEmpty()) return;
    QXmlStreamReader xml(data);

    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) continue;
        if (xml.name() != u"Relationship") continue;
        m_relationships.insert(attribute(xml, u"Id").toString(),
                               attribute(xml, u"Target").toString());
    }
}

QStringList DocxStyles::chainFor(const QString &styleId) const
{
    QStringList chain;
    for (QString id = styleId; !id.isEmpty() && chain.size() < MaxStyleDepth;) {
        const auto it = m_styles.constFind(id);
        if (it == m_styles.constEnd() || chain.contains(id)) break;
        chain.prepend(id);
        id = it->basedOn;
    }
    if (!m_defaultStyleId.isEmpty() && !chain.contains(m_defaultStyleId))
        chain.prepend(m_defaultStyleId);
    return chain;
}

TextStyle DocxStyles::textFor(const QString &styleId) const
{
    TextStyle result = m_defaultText;
    for (const QString &id : chainFor(styleId))
        result.merge(m_styles.value(id).text);
    return result;
}

BlockStyle DocxStyles::blockFor(const QString &styleId) const
{
    BlockStyle result = m_defaultBlock;
    for (const QString &id : chainFor(styleId))
        result.merge(m_styles.value(id).block);
    return result;
}

DocxListRef DocxStyles::listFor(const QString &styleId) const
{
    DocxListRef result;
    for (const QString &id : chainFor(styleId)) {
        const DocxListRef &list = m_styles.value(id).list;
        if (list.numberId > 0) result = list;
    }
    return result;
}

bool DocxStyles::isNumbered(const DocxListRef &list) const
{
    const int abstractId = m_numberToAbstract.value(list.numberId, -1);
    return m_abstractIsNumbered.value(abstractId, false);
}

QString DocxStyles::relationshipTarget(const QString &id) const
{
    return m_relationships.value(id);
}
