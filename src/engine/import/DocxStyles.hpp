#pragma once

#include "engine/import/OfficeStyle.hpp"

#include <QHash>
#include <QString>

QT_BEGIN_NAMESPACE
class QXmlStreamReader;
QT_END_NAMESPACE

class ZipArchive;

/// Where a run or a paragraph in a docx sits in a list, if it does.
struct DocxListRef {
    int numberId { -1 };
    int level    { 0 };
};

/// styles.xml, numbering.xml and the relationships of a docx.
class DocxStyles
{
public:
    void load(const ZipArchive &zip);

    TextStyle   textFor(const QString &styleId) const;
    BlockStyle  blockFor(const QString &styleId) const;

    /// A list a paragraph joins because its style says so, not its own w:numPr.
    DocxListRef listFor(const QString &styleId) const;

    const TextStyle  &defaultText()  const { return m_defaultText; }
    const BlockStyle &defaultBlock() const { return m_defaultBlock; }

    bool    isNumbered(const DocxListRef &list) const;
    QString relationshipTarget(const QString &id) const;

    /// Both are also used for the direct formatting inside document.xml, which
    /// is written with the very same elements as a style is.
    static TextStyle  readRunProperties(QXmlStreamReader &xml);
    static BlockStyle readParagraphProperties(QXmlStreamReader &xml,
                                              QString *styleId = nullptr,
                                              DocxListRef *list = nullptr,
                                              bool *pageBreakBefore = nullptr);

private:
    struct Style {
        QString     basedOn;
        QString     name;
        TextStyle   text;
        BlockStyle  block;
        DocxListRef list;
    };

    QStringList chainFor(const QString &styleId) const;

    void readStyles(const QByteArray &xml);
    void readNumbering(const QByteArray &xml);
    void readRelationships(const QByteArray &xml);

    QHash<QString, Style>   m_styles;
    QHash<int, int>         m_numberToAbstract;
    QHash<int, bool>        m_abstractIsNumbered;
    QHash<QString, QString> m_relationships;

    /// The style Word marks w:default="1"; every paragraph without its own
    /// w:pStyle is formatted by it, and files in the wild rely on that.
    QString    m_defaultStyleId;
    TextStyle  m_defaultText;
    BlockStyle m_defaultBlock;
};
