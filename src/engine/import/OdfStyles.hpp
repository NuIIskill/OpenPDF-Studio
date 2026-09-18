#pragma once

#include "engine/import/OfficeStyle.hpp"

#include <QHash>
#include <QMarginsF>
#include <QSizeF>
#include <QString>

QT_BEGIN_NAMESPACE
class QXmlStreamReader;
QT_END_NAMESPACE

class ZipArchive;

/// The style side of an odt: content.xml's automatic styles, styles.xml's named
/// ones, the list styles and the page geometry.
class OdfStyles
{
public:
    void load(const ZipArchive &zip, const QByteArray &content);

    TextStyle  textFor(const QString &styleName) const;
    BlockStyle blockFor(const QString &styleName) const;
    CellStyle  cellFor(const QString &styleName) const;

    bool   isNumbered(const QString &listStyleName, int level) const;
    double columnWidthPt(const QString &styleName) const;

    QSizeF    pageSizePt() const { return m_pageSizePt; }
    QMarginsF marginsPt()  const { return m_marginsPt; }
    QString   defaultFamily() const;
    double    defaultSizePt() const;

    /// Also used for the direct formatting on a span or a paragraph.
    void readStyleProperties(QXmlStreamReader &xml, TextStyle *text, BlockStyle *block,
                             CellStyle *cell = nullptr);

private:
    struct Style {
        QString    parent;
        QString    family;
        TextStyle  text;
        BlockStyle block;
        CellStyle  cell;
        double     columnWidthPt { 0.0 };
    };

    QStringList chainFor(const QString &styleName) const;

    void readStyleDocument(const QByteArray &data);
    void readStyle(QXmlStreamReader &xml);
    void readListStyle(QXmlStreamReader &xml);
    void readPageLayout(QXmlStreamReader &xml);
    void readFontFace(QXmlStreamReader &xml);

    QHash<QString, Style>   m_styles;
    QHash<QString, QString> m_fontFamilies;
    QHash<QString, bool>    m_listLevels;      // "Name/level" to numbered
    QString                 m_defaultStyle;

    QSizeF    m_pageSizePt { 595.0, 842.0 };
    QMarginsF m_marginsPt  { 56.7, 56.7, 56.7, 56.7 };
};
