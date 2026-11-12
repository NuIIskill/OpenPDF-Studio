#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

/// What the standard scripts of a form field ask of its value: input format, allowed range and calculation.
namespace FormScript {

struct Format {
    enum class Kind { None, Number, Percent, Date, Time, Special };

    Kind    kind { Kind::None };
    int     decimals { 2 };
    int     separators { 0 };
    QString pattern;
    int     special { -1 };
};

struct Range {
    std::optional<double> min;
    std::optional<double> max;
};

enum class Problem { None, NotNumber, NotDate, NotTime, NotSpecial, OutOfRange };

struct Checked {
    Problem problem { Problem::None };
    QString value;
};

Format formatOf(const QString &keystroke, const QString &format);
std::optional<Range> rangeOf(const QString &validate);

Checked check(const QString &input, const Format &format, const std::optional<Range> &range);

std::optional<double> number(const QString &value);
QString numberText(double value, const Format &format);

using Lookup = std::function<QStringList(const QString &field)>;

bool calculates(const QString &script);
std::optional<double> calculate(const QString &script, const Lookup &values);

/// A calculation the standard function does: an operation over a list of fields.
struct Simple {
    QString     operation;
    QStringList fields;
};

std::optional<Simple> simpleOf(const QString &script);

QString keystrokeScript(const Format &format);
QString formatScript(const Format &format);
QString rangeScript(const std::optional<Range> &range);
QString calculationScript(const Simple &simple);

}
