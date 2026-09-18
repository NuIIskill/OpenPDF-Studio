#include "engine/import/OdfStyles.hpp"

#include "engine/import/XmlRead.hpp"
#include "engine/import/ZipArchive.hpp"

#include <QTextCharFormat>
#include <QXmlStreamReader>

using XmlRead::attribute;
using XmlRead::odfLengthToPt;

namespace {

constexpr int MaxStyleDepth = 12;

Qt::Alignment alignmentOf(QStringView value)
{
    if (value == QStringView(u"center")) return Qt::AlignHCenter;
    if (value == QStringView(u"end") || value == QStringView(u"right"))
        return Qt::AlignRight;
    if (value == QStringView(u"justify")) return Qt::AlignJustify;
    return Qt::AlignLeft;
}

bool isOn(QStringView value)
{
    return !value.isEmpty() && value != QStringView(u"none")
        && value != QStringView(u"normal");
}

}

void OdfStyles::readStyleProperties(QXmlStreamReader &xml, TextStyle *text,
                                    BlockStyle *block, CellStyle *cell)
{
    const QStringView element = xml.name();

    if (element == u"table-cell-properties" && cell) {
        const QColor background = XmlRead::hexColor(attribute(xml, u"background-color"));
        if (background.isValid()) cell->background = background;

        for (const QStringView side : { QStringView(u"border"), QStringView(u"border-top"),
                                        QStringView(u"border-left") }) {
            double width = 0.0;
            QColor color;
            if (!XmlRead::odfBorder(attribute(xml, side), &width, &color)) continue;
            cell->borderWidthPt = width;
            if (color.isValid()) cell->borderColor = color;
            break;
        }
        return;
    }

    if (element == u"text-properties" && text) {
        const QStringView weight = attribute(xml, u"font-weight");
        if (!weight.isEmpty())
            text->bold = weight == QStringView(u"bold") || weight.toInt() >= 600;

        const QStringView style = attribute(xml, u"font-style");
        if (!style.isEmpty()) text->italic = isOn(style);

        if (XmlRead::hasAttribute(xml, u"text-underline-style"))
            text->underline = isOn(attribute(xml, u"text-underline-style"));
        if (XmlRead::hasAttribute(xml, u"text-line-through-style"))
            text->strikeOut = isOn(attribute(xml, u"text-line-through-style"));

        const QStringView size = attribute(xml, u"font-size");
        if (!size.isEmpty() && !size.endsWith(QStringView(u"%"))) {
            const double points = odfLengthToPt(size);
            if (points > 0.0) text->sizePt = points;
        }

        const QColor color = XmlRead::hexColor(attribute(xml, u"color"));
        if (color.isValid()) text->color = color;

        QStringView family = attribute(xml, u"font-family");
        if (family.isEmpty()) {
            const QString named = attribute(xml, u"font-name").toString();
            if (!named.isEmpty() && m_fontFamilies.contains(named))
                text->family = m_fontFamilies.value(named);
        } else {
            QString name = family.toString();
            name.remove(QLatin1Char('\''));
            text->family = name;
        }

        const QStringView position = attribute(xml, u"text-position");
        if (position.startsWith(QStringView(u"super")))
            text->verticalAlign = QTextCharFormat::AlignSuperScript;
        else if (position.startsWith(QStringView(u"sub")))
            text->verticalAlign = QTextCharFormat::AlignSubScript;

    } else if (element == u"paragraph-properties" && block) {
        const QStringView align = attribute(xml, u"text-align");
        if (!align.isEmpty()) block->alignment = alignmentOf(align);

        if (XmlRead::hasAttribute(xml, u"margin-left"))
            block->leftIndentPt = odfLengthToPt(attribute(xml, u"margin-left"));
        if (XmlRead::hasAttribute(xml, u"margin-right"))
            block->rightIndentPt = odfLengthToPt(attribute(xml, u"margin-right"));
        if (XmlRead::hasAttribute(xml, u"text-indent"))
            block->firstLineIndentPt = odfLengthToPt(attribute(xml, u"text-indent"));
        if (XmlRead::hasAttribute(xml, u"margin-top"))
            block->spaceBeforePt = odfLengthToPt(attribute(xml, u"margin-top"));
        if (XmlRead::hasAttribute(xml, u"margin-bottom"))
            block->spaceAfterPt = odfLengthToPt(attribute(xml, u"margin-bottom"));

        const QStringView lineHeight = attribute(xml, u"line-height");
        if (lineHeight.endsWith(QStringView(u"%"))) {
            const double percent = lineHeight.chopped(1).toDouble();
            if (percent > 0.0) block->lineHeightPercent = percent;
        } else if (!lineHeight.isEmpty() && lineHeight != QStringView(u"normal")) {
            const double points = odfLengthToPt(lineHeight);
            if (points > 0.0) block->lineHeightExactPt = points;
        }
        const double atLeast = odfLengthToPt(attribute(xml, u"line-height-at-least"));
        if (atLeast > 0.0) block->lineHeightMinimumPt = atLeast;

        const QColor background = XmlRead::hexColor(attribute(xml, u"background-color"));
        if (background.isValid()) block->background = background;

        if (attribute(xml, u"break-before") == QStringView(u"page"))
            block->pageBreakBefore = true;
    }
}

void OdfStyles::readStyle(QXmlStreamReader &xml)
{
    const QString name = attribute(xml, u"name").toString();

    Style style;
    style.parent = attribute(xml, u"parent-style-name").toString();
    style.family = attribute(xml, u"family").toString();

    while (xml.readNextStartElement()) {
        if (xml.name() == u"table-column-properties") {
            style.columnWidthPt = odfLengthToPt(attribute(xml, u"column-width"));
            // A relative width such as "21845*" is a share, not a length. Only
            // the ratio between the columns is used, and a table built from
            // shares fills the text width, which is what ODF means by it.
            const QStringView share = attribute(xml, u"rel-column-width");
            if (style.columnWidthPt <= 0.0 && share.endsWith(u'*'))
                style.columnWidthPt = share.chopped(1).toDouble();
        }
        readStyleProperties(xml, &style.text, &style.block, &style.cell);
        xml.skipCurrentElement();
    }
    if (!name.isEmpty()) m_styles.insert(name, style);
}

void OdfStyles::readListStyle(QXmlStreamReader &xml)
{
    const QString name = attribute(xml, u"name").toString();
    while (xml.readNextStartElement()) {
        const bool numbered = xml.name() == u"list-level-style-number";
        const int  level    = qMax(1, attribute(xml, u"level").toInt());
        m_listLevels.insert(QStringLiteral("%1/%2").arg(name).arg(level - 1), numbered);
        xml.skipCurrentElement();
    }
}

void OdfStyles::readPageLayout(QXmlStreamReader &xml)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == u"page-layout-properties") {
            const QSizeF size(odfLengthToPt(attribute(xml, u"page-width")),
                              odfLengthToPt(attribute(xml, u"page-height")));
            if (size.width() > 1.0 && size.height() > 1.0) m_pageSizePt = size;

            m_marginsPt = QMarginsF(odfLengthToPt(attribute(xml, u"margin-left")),
                                    odfLengthToPt(attribute(xml, u"margin-top")),
                                    odfLengthToPt(attribute(xml, u"margin-right")),
                                    odfLengthToPt(attribute(xml, u"margin-bottom")));
        }
        xml.skipCurrentElement();
    }
}

void OdfStyles::readFontFace(QXmlStreamReader &xml)
{
    QString family = attribute(xml, u"font-family").toString();
    family.remove(QLatin1Char('\''));
    if (!family.isEmpty())
        m_fontFamilies.insert(attribute(xml, u"name").toString(), family);
}

void OdfStyles::readStyleDocument(const QByteArray &data)
{
    if (data.isEmpty()) return;
    QXmlStreamReader xml(data);

    // Style definitions sit in several containers and in two files; the walk
    // looks for the definitions themselves rather than the boxes around them.
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) continue;

        const QStringView name = xml.name();
        if      (name == u"font-face")   readFontFace(xml);
        else if (name == u"style")       readStyle(xml);
        else if (name == u"list-style")  readListStyle(xml);
        else if (name == u"page-layout") readPageLayout(xml);
        else if (name == u"default-style") {
            Style style;
            style.family = attribute(xml, u"family").toString();
            while (xml.readNextStartElement()) {
                readStyleProperties(xml, &style.text, &style.block);
                xml.skipCurrentElement();
            }
            if (style.family == QLatin1String("paragraph")) {
                m_defaultStyle = QStringLiteral("__default__");
                m_styles.insert(m_defaultStyle, style);
            }
        }
        else if (name == u"body") break;   // the styles are all in front of it
    }
}

void OdfStyles::load(const ZipArchive &zip, const QByteArray &content)
{
    readStyleDocument(zip.read(QStringLiteral("styles.xml")));
    readStyleDocument(content);
}

QStringList OdfStyles::chainFor(const QString &styleName) const
{
    QStringList chain;
    for (QString name = styleName; !name.isEmpty() && chain.size() < MaxStyleDepth;) {
        const auto it = m_styles.constFind(name);
        if (it == m_styles.constEnd() || chain.contains(name)) break;
        chain.prepend(name);
        name = it->parent;
    }
    return chain;
}

TextStyle OdfStyles::textFor(const QString &styleName) const
{
    TextStyle result = m_styles.value(m_defaultStyle).text;
    for (const QString &name : chainFor(styleName))
        result.merge(m_styles.value(name).text);
    return result;
}

BlockStyle OdfStyles::blockFor(const QString &styleName) const
{
    BlockStyle result = m_styles.value(m_defaultStyle).block;
    for (const QString &name : chainFor(styleName))
        result.merge(m_styles.value(name).block);
    return result;
}

CellStyle OdfStyles::cellFor(const QString &styleName) const
{
    CellStyle result;
    for (const QString &name : chainFor(styleName))
        result.merge(m_styles.value(name).cell);
    return result;
}

bool OdfStyles::isNumbered(const QString &listStyleName, int level) const
{
    return m_listLevels.value(QStringLiteral("%1/%2").arg(listStyleName).arg(level),
                              false);
}

QString OdfStyles::defaultFamily() const
{
    return m_styles.value(m_defaultStyle).text.family.value_or(QString());
}

double OdfStyles::columnWidthPt(const QString &styleName) const
{
    return m_styles.value(styleName).columnWidthPt;
}

double OdfStyles::defaultSizePt() const
{
    return m_styles.value(m_defaultStyle).text.sizePt.value_or(0.0);
}
