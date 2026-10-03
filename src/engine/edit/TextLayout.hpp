#pragma once

#include "engine/edit/TextBoxProperties.hpp"

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

struct Metrics {
    double              sizePt { 0.0 };
    QHash<uint, double> advances;
    QList<OriginalLine> originals;
    QPointF             boxTopLeft;

    bool   isValid() const { return sizePt > 0.0; }
    double advance(uint codePoint) const { return advances.value(codePoint, 0.0); }
};

struct Params {
    double widthPt            { 0.0 };
    double lineStepPt         { 0.0 };
    double paragraphSpacingPt { 0.0 };
    double charSpacingPt      { 0.0 };
    TextBoxProperties::ListStyle      list  { TextBoxProperties::ListStyle::None };
    TextBoxProperties::HorizontalAlign align { TextBoxProperties::HorizontalAlign::Left };
};

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
    int     original  { -1 };
    bool    keep      { false };
    int     keptCount  { 0 };
    int     fixedCount { 0 };
    QList<double> fixedX;
};

struct Result {
    QList<Line> lines;
    QPointF     firstBaseline;
    bool        anchored { false };
};

QString markerFor(TextBoxProperties::ListStyle list, int paragraph);

QString measuredCharacters(const QString &text, TextBoxProperties::ListStyle list);

QList<Line> lay(const QString &text, const Params &params,
                const std::function<double(char32_t)> &advance);

double lineStep(double sizePt, double lineSpacingPt);

QPointF firstBaseline(const TextBoxProperties &box, const QSizeF &bounds, double sizePt,
                      const QList<Line> &lines, bool hasTextOrigin,
                      const QPointF &textOriginOffset);

double textWidth(const TextBoxProperties &box, const QSizeF &bounds,
                 const QPointF &firstBaseline);

int commonPrefix(const OriginalLine &original, QStringView line);

int keptPrefix(const OriginalLine &original, QStringView line);

double restartX(const OriginalLine &original, int kept);

Result layout(const QString &text, const Params &params, const TextBoxProperties &box,
              const QRectF &bounds, double sizePt, bool wraps, bool hasTextOrigin,
              const QPointF &textOriginOffset, const QList<OriginalLine> &originals,
              const std::function<double(char32_t)> &advance);

double xAt(const Line &line, const QString &text, int position, double charSpacing,
           const std::function<double(char32_t)> &advance);

QString withSoftBreaks(const QString &text, const QList<Line> &lines);

QString withoutSoftBreaks(QString text);

}
