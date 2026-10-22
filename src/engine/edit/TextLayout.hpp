#pragma once

#include "engine/edit/TextBoxProperties.hpp"

#include <QColor>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <functional>

/// Lays out edited text into lines, the same way for the PDF writer and the editor.
namespace TextLayout {

inline constexpr char16_t kSoftBreak = 0x2028;

struct OriginalLine {
    QRectF        rect;
    QString       text;
    double        baseline { 0.0 };
    QList<double> charX;
    QList<bool>   objectStart;
    double        endX { 0.0 };

    bool operator==(const OriginalLine &) const = default;
};

using Advance = std::function<double(int position, char32_t codePoint)>;

QString originalText(const QList<OriginalLine> &originals);

QList<int> styleIndices(const QString &text, const QString &original,
                        const QList<int> &originalStyles);

int dominantStyle(const QList<int> &styles, int count);

// A character of the open document whose style a pasted character takes.
struct Source {
    int     page { -1 };
    QPointF at;
    double  rise { 0.0 };

    bool isValid() const { return page >= 0; }
    bool operator==(const Source &) const = default;
};

QList<Source> distinctSources(const QList<Source> &sources);
QList<Source> alignSources(const QString &text, const QList<Source> &plain);

// How the characters of one style look, as the format bar shows them.
struct Appearance {
    QString family;
    double  sizePt { 0.0 };
    bool    bold   { false };
    bool    italic { false };
    QColor  color;

    bool isValid() const { return sizePt > 0.0; }
    bool operator==(const Appearance &) const = default;
};

struct Metrics {
    QList<double>             sizes;
    QList<QHash<uint, double>> advances;
    QString                   original;
    QList<int>                originalStyles;
    QList<OriginalLine>       originals;
    bool                      anchor { false };
    QPointF                   boxTopLeft;
    QList<Source>             sources;
    QList<int>                sourceStyles;
    QList<Source>             styleSources;
    QList<Appearance>         appearances;

    bool isValid() const { return !advances.isEmpty() && sizes.size() == advances.size(); }
    bool covers(const QString &text, const QList<Source> &chars = {}) const;
    QList<int> stylesOf(const QString &text, const QList<Source> &chars = {}) const;
    double sizeFor(const QList<int> &styles) const;
    Advance advanceFor(const QList<int> &styles) const;
};

struct Params {
    double widthPt            { 0.0 };
    double alignWidthPt       { 0.0 };
    double lineStepPt         { 0.0 };
    double paragraphSpacingPt { 0.0 };
    double charSpacingPt      { 0.0 };
    bool   anchor             { false };
    bool   keep               { false };
    TextBoxProperties::ListStyle      list  { TextBoxProperties::ListStyle::None };
    TextBoxProperties::HorizontalAlign align { TextBoxProperties::HorizontalAlign::Left };
};

bool anchors(const TextBoxProperties &box);

struct Line {
    int     start     { 0 };
    int     length    { 0 };
    int     paragraph { 0 };
    bool    first     { false };
    QString prefix;
    double  prefixWidth { 0.0 };
    double  x         { 0.0 };
    double  y         { 0.0 };
    double  width     { 0.0 };
    double  spaceExtra { 0.0 };
    double  alignShift { 0.0 };
    int     original  { -1 };
    bool    keep      { false };
    int     keptCount  { 0 };
    int     fixedCount { 0 };
    QList<double> fixedX;
    int     tailCount { 0 };
    QList<double> tailX;
    double  tailShift { 0.0 };
};

struct Result {
    QList<Line> lines;
    QPointF     firstBaseline;
    bool        anchored { false };
};

QString markerFor(TextBoxProperties::ListStyle list, int paragraph);

QString measuredCharacters(const QString &text, TextBoxProperties::ListStyle list);

QList<Line> lay(const QString &text, const Params &params, const Advance &advance);

double lineStep(double sizePt, double lineSpacingPt);

QPointF firstBaseline(const TextBoxProperties &box, const QSizeF &bounds, double sizePt,
                      const QList<Line> &lines, bool hasTextOrigin,
                      const QPointF &textOriginOffset);

double textWidth(const TextBoxProperties &box, const QSizeF &bounds,
                 const QPointF &firstBaseline);

double indentPt(const TextBoxProperties &box, const QSizeF &bounds, double sizePt);

int commonPrefix(const OriginalLine &original, QStringView line);

int commonSuffix(const OriginalLine &original, QStringView line, int prefix);

int keptPrefix(const OriginalLine &original, QStringView line);

double restartX(const OriginalLine &original, int kept);

Result layout(const QString &text, const Params &params, const TextBoxProperties &box,
              const QRectF &bounds, double sizePt, bool wraps, bool hasTextOrigin,
              const QPointF &textOriginOffset, const QList<OriginalLine> &originals,
              const Advance &advance);

double xAt(const Line &line, const QString &text, int position, double charSpacing,
           const Advance &advance);

QString withSoftBreaks(const QString &text, const QList<Line> &lines);

QString withoutSoftBreaks(QString text);

}
