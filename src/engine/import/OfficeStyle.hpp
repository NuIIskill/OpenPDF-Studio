#pragma once

#include <QColor>
#include <QMarginsF>
#include <QString>
#include <Qt>

#include <optional>

/// Character and paragraph properties as docx and odt both express them.
///
/// Every field is optional so that a style and the direct formatting on top of
/// it merge by overwriting only what the outer level actually states.
struct TextStyle {
    std::optional<bool>    bold;
    std::optional<bool>    italic;
    std::optional<bool>    underline;
    std::optional<bool>    strikeOut;
    std::optional<double>  sizePt;
    std::optional<QString> family;
    std::optional<QColor>  color;
    std::optional<int>     verticalAlign;   // QTextCharFormat::VerticalAlignment

    void merge(const TextStyle &over)
    {
        if (over.bold)          bold          = over.bold;
        if (over.italic)        italic        = over.italic;
        if (over.underline)     underline     = over.underline;
        if (over.strikeOut)     strikeOut     = over.strikeOut;
        if (over.sizePt)        sizePt        = over.sizePt;
        if (over.family)        family        = over.family;
        if (over.color)         color         = over.color;
        if (over.verticalAlign) verticalAlign = over.verticalAlign;
    }
};

struct BlockStyle {
    std::optional<Qt::Alignment> alignment;
    std::optional<double>        leftIndentPt;
    std::optional<double>        rightIndentPt;
    std::optional<double>        firstLineIndentPt;
    std::optional<double>        spaceBeforePt;
    std::optional<double>        spaceAfterPt;
    std::optional<double>        lineHeightPercent;
    std::optional<double>        lineHeightExactPt;
    std::optional<double>        lineHeightMinimumPt;
    std::optional<int>           headingLevel;
    std::optional<QColor>        background;
    std::optional<bool>          pageBreakBefore;

    void merge(const BlockStyle &over)
    {
        if (over.alignment)         alignment         = over.alignment;
        if (over.leftIndentPt)      leftIndentPt      = over.leftIndentPt;
        if (over.rightIndentPt)     rightIndentPt     = over.rightIndentPt;
        if (over.firstLineIndentPt) firstLineIndentPt = over.firstLineIndentPt;
        if (over.spaceBeforePt)     spaceBeforePt     = over.spaceBeforePt;
        if (over.spaceAfterPt)      spaceAfterPt      = over.spaceAfterPt;
        if (over.lineHeightPercent) lineHeightPercent = over.lineHeightPercent;
        if (over.lineHeightExactPt) lineHeightExactPt = over.lineHeightExactPt;
        if (over.lineHeightMinimumPt) lineHeightMinimumPt = over.lineHeightMinimumPt;
        if (over.headingLevel)      headingLevel      = over.headingLevel;
        if (over.background)        background        = over.background;
        if (over.pageBreakBefore)   pageBreakBefore   = over.pageBreakBefore;
    }
};

/// Borders and shading of a table and of one cell.
struct TableStyle {
    std::optional<QColor> borderColor;
    std::optional<double> borderWidthPt;
};

struct CellStyle {
    std::optional<QColor>    background;
    std::optional<QColor>    borderColor;
    std::optional<double>    borderWidthPt;
    std::optional<QMarginsF> paddingPt;

    void merge(const CellStyle &over)
    {
        if (over.background)    background    = over.background;
        if (over.borderColor)   borderColor   = over.borderColor;
        if (over.borderWidthPt) borderWidthPt = over.borderWidthPt;
        if (over.paddingPt)     paddingPt     = over.paddingPt;
    }
};

/// One level of a bullet or numbered list.
struct ListStyle {
    bool numbered { false };
    int  level    { 0 };

    bool operator==(const ListStyle &other) const
    { return numbered == other.numbered && level == other.level; }
};
