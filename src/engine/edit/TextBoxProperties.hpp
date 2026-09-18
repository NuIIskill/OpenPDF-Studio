#pragma once

#include <QColor>
#include <QRectF>

/// Stores the serializable appearance and layout of an editable text box.
struct TextBoxProperties
{
    enum class VerticalAlign { Top, Center, Bottom };
    enum class HorizontalAlign { Left, Center, Right, Justify };
    enum class BorderStyle   { Solid, Dashed, Dotted };
    enum class ListStyle     { None, Bullets, Numbered };

    QRectF bounds;
    bool autoHeight { true };
    double paddingPt { 0.0 };
    VerticalAlign verticalAlign { VerticalAlign::Top };
    HorizontalAlign horizontalAlign { HorizontalAlign::Left };
    ListStyle listStyle { ListStyle::None };
    int indentLevel { 0 };
    double rotationDeg { 0.0 };
    double characterSpacingPt { 0.0 };
    double paragraphSpacingPt { 0.0 };
    double lineSpacingMultiplier { 0.0 };
    double opacity { 1.0 };
    double cornerRadiusPt { 0.0 };
    bool borderEnabled { false };
    BorderStyle borderStyle { BorderStyle::Solid };
    double borderWidthPt { 1.0 };
    QColor borderColor { Qt::black };
    bool backgroundEnabled { false };
    QColor backgroundColor { Qt::white };

    bool operator==(const TextBoxProperties &o) const
    {
        return bounds == o.bounds && autoHeight == o.autoHeight
            && qFuzzyCompare(paddingPt + 1.0, o.paddingPt + 1.0)
            && verticalAlign == o.verticalAlign
            && horizontalAlign == o.horizontalAlign
            && listStyle == o.listStyle && indentLevel == o.indentLevel
            && qFuzzyCompare(rotationDeg + 1.0, o.rotationDeg + 1.0)
            && qFuzzyCompare(characterSpacingPt + 1.0, o.characterSpacingPt + 1.0)
            && qFuzzyCompare(paragraphSpacingPt + 1.0, o.paragraphSpacingPt + 1.0)
            && qFuzzyCompare(lineSpacingMultiplier + 1.0, o.lineSpacingMultiplier + 1.0)
            && qFuzzyCompare(opacity + 1.0, o.opacity + 1.0)
            && qFuzzyCompare(cornerRadiusPt + 1.0, o.cornerRadiusPt + 1.0)
            && borderEnabled == o.borderEnabled && borderStyle == o.borderStyle
            && qFuzzyCompare(borderWidthPt + 1.0, o.borderWidthPt + 1.0)
            && borderColor == o.borderColor
            && backgroundEnabled == o.backgroundEnabled
            && backgroundColor == o.backgroundColor;
    }
    bool operator!=(const TextBoxProperties &o) const { return !(*this == o); }
};
