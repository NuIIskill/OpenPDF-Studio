#include "engine/forms/FormScript.hpp"

#include <QDateTime>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

namespace FormScript {

namespace {

const QStringList kDateFormats = {
    QStringLiteral("m/d"), QStringLiteral("m/d/yy"), QStringLiteral("mm/dd/yy"),
    QStringLiteral("mm/yy"), QStringLiteral("d-mmm"), QStringLiteral("d-mmm-yy"),
    QStringLiteral("dd-mmm-yy"), QStringLiteral("yy-mm-dd"), QStringLiteral("mmm-yy"),
    QStringLiteral("mmmm-yy"), QStringLiteral("mmm d, yyyy"), QStringLiteral("mmmm d, yyyy"),
    QStringLiteral("m/d/yy h:MM tt"), QStringLiteral("m/d/yy HH:MM"),
};

const QStringList kTimeFormats = {
    QStringLiteral("HH:MM"), QStringLiteral("h:MM tt"),
    QStringLiteral("HH:MM:ss"), QStringLiteral("h:MM:ss tt"),
};

QRegularExpressionMatch find(const QString &script, const QString &pattern)
{
    return QRegularExpression(pattern).match(script);
}

// Acrobat writes months as m and minutes as M, Qt the other way round.
QString qtPattern(const QString &acrobat)
{
    QString out;
    for (int i = 0; i < acrobat.size();) {
        const QChar c = acrobat.at(i);
        int run = 1;
        while (i + run < acrobat.size() && acrobat.at(i + run) == c) ++run;
        if (c == u'm')                                  out += QString(run, u'M');
        else if (c == u'M')                             out += QString(run, u'm');
        else if (c == u't')                             out += QStringLiteral("AP");
        else if (QStringLiteral("dyHhs").contains(c))   out += QString(run, c);
        else if (c.isLetter())                          out += u'\'' + QString(run, c) + u'\'';
        else                                            out += QString(run, c);
        i += run;
    }
    return out;
}

std::optional<QDateTime> readDate(const QString &input, const QString &pattern, bool time)
{
    const QLocale c = QLocale::c();
    QStringList tries { qtPattern(pattern) };
    if (time)
        tries << QStringLiteral("H:mm") << QStringLiteral("H:mm:ss") << QStringLiteral("h:mm AP");
    else
        tries << QStringLiteral("d.M.yyyy") << QStringLiteral("d.M.yy") << QStringLiteral("yyyy-MM-dd")
              << QStringLiteral("d-M-yyyy") << QStringLiteral("d/M/yyyy");
    for (const QString &format : std::as_const(tries)) {
        QDateTime read = time ? QDateTime(QDate(2000, 1, 1), c.toTime(input, format))
                              : c.toDateTime(input, format);
        if (!read.isValid()) continue;
        // Two-digit years are read as 19xx.
        if (!time && !format.contains(QLatin1String("yyyy")) && format.contains(QLatin1String("yy"))
                && read.date().year() < 1930)
            read = read.addYears(100);
        return read;
    }
    return std::nullopt;
}

QChar decimalPoint(const Format &format)
{
    return format.separators == 2 || format.separators == 3 ? u',' : u'.';
}

QStringList quotedNames(const QString &list)
{
    QStringList names;
    static const QRegularExpression quoted(QStringLiteral(R"((["'])(.*?)\1)"));
    for (auto it = quoted.globalMatch(list); it.hasNext();) names << it.next().captured(2);
    if (names.size() == 1 && names.first().contains(u','))
        names = names.first().split(u',', Qt::SkipEmptyParts);
    for (QString &name : names) name = name.trimmed();
    return names;
}

double fieldNumber(const QStringList &values)
{
    double total = 0;
    for (const QString &v : values) total += number(v).value_or(0.0);
    return total;
}

/// Reads the arithmetic of a hand-written calculation script.
class Expression
{
public:
    Expression(QString text, const Lookup &values, const QHash<QString, double> &vars)
        : m_s(std::move(text)), m_values(values), m_vars(vars) {}

    std::optional<double> parse()
    {
        const auto v = sum();
        skip();
        if (m_i != m_s.size()) return std::nullopt;
        return v;
    }

private:
    void skip() { while (m_i < m_s.size() && m_s.at(m_i).isSpace()) ++m_i; }

    bool eat(QChar c)
    {
        skip();
        if (m_i < m_s.size() && m_s.at(m_i) == c) { ++m_i; return true; }
        return false;
    }

    std::optional<double> sum()
    {
        auto v = term();
        while (v) {
            if (eat(u'+'))      { const auto r = term(); v = r ? std::optional(*v + *r) : std::nullopt; }
            else if (eat(u'-')) { const auto r = term(); v = r ? std::optional(*v - *r) : std::nullopt; }
            else break;
        }
        return v;
    }

    std::optional<double> term()
    {
        auto v = unary();
        while (v) {
            if (eat(u'*'))      { const auto r = unary(); v = r ? std::optional(*v * *r) : std::nullopt; }
            else if (eat(u'/')) { const auto r = unary(); v = r ? std::optional(*v / *r) : std::nullopt; }
            else break;
        }
        return v;
    }

    std::optional<double> unary()
    {
        if (eat(u'-')) { const auto v = unary(); return v ? std::optional(-*v) : std::nullopt; }
        if (eat(u'+')) return unary();
        return primary();
    }

    std::optional<QString> string()
    {
        skip();
        if (m_i >= m_s.size() || (m_s.at(m_i) != u'"' && m_s.at(m_i) != u'\'')) return std::nullopt;
        const QChar quote = m_s.at(m_i++);
        const int end = m_s.indexOf(quote, m_i);
        if (end < 0) return std::nullopt;
        const QString text = m_s.mid(m_i, end - m_i);
        m_i = end + 1;
        return text;
    }

    QString path()
    {
        skip();
        const int start = m_i;
        while (m_i < m_s.size() && (m_s.at(m_i).isLetterOrNumber() || m_s.at(m_i) == u'_'
                                    || m_s.at(m_i) == u'.' || m_s.at(m_i) == u'$'))
            ++m_i;
        return m_s.mid(start, m_i - start);
    }

    QList<std::optional<double>> arguments()
    {
        QList<std::optional<double>> args;
        if (!eat(u'(')) return { std::nullopt };
        if (eat(u')')) return args;
        do args << sum(); while (eat(u','));
        if (!eat(u')')) args << std::nullopt;
        return args;
    }

    std::optional<double> primary()
    {
        if (eat(u'(')) {
            const auto v = sum();
            return eat(u')') ? v : std::nullopt;
        }
        skip();
        if (m_i < m_s.size() && (m_s.at(m_i).isDigit() || m_s.at(m_i) == u'.')) {
            const int start = m_i;
            while (m_i < m_s.size() && (m_s.at(m_i).isDigit() || m_s.at(m_i) == u'.')) ++m_i;
            bool ok = false;
            const double v = m_s.mid(start, m_i - start).toDouble(&ok);
            return ok ? std::optional(v) : std::nullopt;
        }

        const QString name = path();
        if (name.isEmpty()) return std::nullopt;
        if (name == QLatin1String("this.getField") || name == QLatin1String("getField")) {
            if (!eat(u'(')) return std::nullopt;
            const auto field = string();
            if (!field || !eat(u')')) return std::nullopt;
            const QString property = path();
            if (property != QLatin1String(".value") && property != QLatin1String(".valueAsString"))
                return std::nullopt;
            return fieldNumber(m_values(*field).mid(0, 1));
        }

        static const QStringList oneArgument = {
            QStringLiteral("Number"), QStringLiteral("parseFloat"), QStringLiteral("AFMakeNumber"),
            QStringLiteral("Math.round"), QStringLiteral("Math.abs"), QStringLiteral("Math.floor"),
            QStringLiteral("Math.ceil"),
        };
        if (oneArgument.contains(name)) {
            const auto args = arguments();
            if (args.size() != 1 || !args.first()) return std::nullopt;
            const double v = *args.first();
            if (name == QLatin1String("Math.round")) return std::floor(v + 0.5);
            if (name == QLatin1String("Math.abs"))   return std::abs(v);
            if (name == QLatin1String("Math.floor")) return std::floor(v);
            if (name == QLatin1String("Math.ceil"))  return std::ceil(v);
            return v;
        }
        if (name == QLatin1String("Math.min") || name == QLatin1String("Math.max")) {
            const auto args = arguments();
            if (args.isEmpty()) return std::nullopt;
            double v = name == QLatin1String("Math.min") ? INFINITY : -INFINITY;
            for (const auto &a : args) {
                if (!a) return std::nullopt;
                v = name == QLatin1String("Math.min") ? std::min(v, *a) : std::max(v, *a);
            }
            return v;
        }
        const auto var = m_vars.constFind(name);
        return var != m_vars.cend() ? std::optional(*var) : std::nullopt;
    }

    QString                       m_s;
    int                           m_i { 0 };
    const Lookup                 &m_values;
    const QHash<QString, double> &m_vars;
};

QString withoutComments(const QString &script)
{
    static const QRegularExpression comments(QStringLiteral(R"(/\*.*?\*/|//[^\n]*)"),
                                             QRegularExpression::DotMatchesEverythingOption);
    return QString(script).remove(comments);
}

QStringList statements(const QString &script)
{
    QStringList out;
    QString current;
    QChar quote;
    for (const QChar c : script) {
        if (!quote.isNull()) {
            if (c == quote) quote = QChar();
        } else if (c == u'"' || c == u'\'') {
            quote = c;
        } else if (c == u';' || c == u'\n' || c == u'\r') {
            if (!current.trimmed().isEmpty()) out << current.trimmed();
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.trimmed().isEmpty()) out << current.trimmed();
    return out;
}

int assignment(const QString &statement)
{
    for (int i = 0; i < statement.size(); ++i) {
        if (statement.at(i) != u'=') continue;
        const bool comparison = (i + 1 < statement.size() && statement.at(i + 1) == u'=')
                             || (i > 0 && QStringLiteral("!<>=").contains(statement.at(i - 1)));
        if (!comparison) return i;
    }
    return -1;
}

std::optional<double> simpleCalculate(const QRegularExpressionMatch &m, const Lookup &values)
{
    const QString op = m.captured(1).toUpper();
    const QStringList names = quotedNames(m.captured(2));
    if (names.isEmpty()) return std::nullopt;
    QList<double> numbers;
    for (const QString &name : names)
        for (const QString &v : values(name)) numbers << number(v).value_or(0.0);
    if (numbers.isEmpty()) return 0.0;
    if (op == QLatin1String("SUM")) return std::accumulate(numbers.cbegin(), numbers.cend(), 0.0);
    if (op == QLatin1String("AVG"))
        return std::accumulate(numbers.cbegin(), numbers.cend(), 0.0) / numbers.size();
    if (op == QLatin1String("PRD"))
        return std::accumulate(numbers.cbegin(), numbers.cend(), 1.0, std::multiplies<double>());
    if (op == QLatin1String("MIN")) return *std::min_element(numbers.cbegin(), numbers.cend());
    if (op == QLatin1String("MAX")) return *std::max_element(numbers.cbegin(), numbers.cend());
    return std::nullopt;
}

const QString kSimple = QStringLiteral(
    R"(AFSimple_Calculate\s*\(\s*["'](\w+)["']\s*,\s*(new\s+Array\s*\([^)]*\)|\[[^\]]*\]|"[^"]*"|'[^']*'))");

}

Format formatOf(const QString &keystroke, const QString &format)
{
    const QString script = format + u'\n' + keystroke;
    Format f;
    QRegularExpressionMatch m;
    if ((m = find(script, QStringLiteral(R"(AFNumber_(?:Format|Keystroke)\s*\(\s*(-?\d+)\s*,\s*(\d+))"))).hasMatch()) {
        f.kind       = Format::Kind::Number;
        f.decimals   = qBound(0, m.captured(1).toInt(), 10);
        f.separators = m.captured(2).toInt();
    } else if ((m = find(script, QStringLiteral(R"(AFPercent_(?:Format|Keystroke)\s*\(\s*(\d+)\s*,\s*(\d+))"))).hasMatch()) {
        f.kind       = Format::Kind::Percent;
        f.decimals   = qBound(0, m.captured(1).toInt(), 10);
        f.separators = m.captured(2).toInt();
    } else if ((m = find(script, QStringLiteral(R"(AFDate_(?:Format|Keystroke)Ex\s*\(\s*["']([^"']*)["'])"))).hasMatch()) {
        f.kind    = Format::Kind::Date;
        f.pattern = m.captured(1);
    } else if ((m = find(script, QStringLiteral(R"(AFDate_(?:Format|Keystroke)\s*\(\s*(\d+))"))).hasMatch()) {
        f.kind    = Format::Kind::Date;
        f.pattern = kDateFormats.value(m.captured(1).toInt(), kDateFormats.first());
    } else if ((m = find(script, QStringLiteral(R"(AFTime_(?:Format|Keystroke)Ex\s*\(\s*["']([^"']*)["'])"))).hasMatch()) {
        f.kind    = Format::Kind::Time;
        f.pattern = m.captured(1);
    } else if ((m = find(script, QStringLiteral(R"(AFTime_(?:Format|Keystroke)\s*\(\s*(\d+))"))).hasMatch()) {
        f.kind    = Format::Kind::Time;
        f.pattern = kTimeFormats.value(m.captured(1).toInt(), kTimeFormats.first());
    } else if ((m = find(script, QStringLiteral(R"(AFSpecial_(?:Format|Keystroke)\s*\(\s*(\d+))"))).hasMatch()) {
        f.kind    = Format::Kind::Special;
        f.special = m.captured(1).toInt();
    }
    return f;
}

std::optional<Range> rangeOf(const QString &validate)
{
    const auto m = find(validate, QStringLiteral(
        R"(AFRange_Validate\s*\(\s*(true|false)\s*,\s*(-?[\d.]+)\s*,\s*(true|false)\s*,\s*(-?[\d.]+))"));
    if (!m.hasMatch()) return std::nullopt;
    Range r;
    if (m.captured(1) == QLatin1String("true")) r.min = m.captured(2).toDouble();
    if (m.captured(3) == QLatin1String("true")) r.max = m.captured(4).toDouble();
    if (!r.min && !r.max) return std::nullopt;
    return r;
}

std::optional<double> number(const QString &value)
{
    QString s;
    for (const QChar c : value)
        if (c.isDigit() || c == u'.' || c == u',' || c == u'-' || c == u'+') s += c;
    if (s.isEmpty() || value.trimmed().isEmpty()) return std::nullopt;
    const int dot = s.lastIndexOf(u'.'), comma = s.lastIndexOf(u',');
    // The separator written last is the decimal one; the other only groups digits.
    if (dot >= 0 && comma >= 0) {
        s.remove(dot > comma ? u',' : u'.');
        s.replace(u',', u'.');
    } else if (comma >= 0) {
        s.replace(u',', u'.');
        if (s.count(u'.') > 1) s.remove(u'.');
    } else if (s.count(u'.') > 1) {
        s.remove(u'.');
    }
    bool ok = false;
    const double v = s.toDouble(&ok);
    return ok && std::isfinite(v) ? std::optional(v) : std::nullopt;
}

QString numberText(double value, const Format &format)
{
    QString text;
    if (format.kind == Format::Kind::Number) {
        text = QString::number(value, 'f', format.decimals);
    } else {
        text = QString::number(value, 'f', 10);
        if (text.contains(u'.')) {
            while (text.endsWith(u'0')) text.chop(1);
            if (text.endsWith(u'.')) text.chop(1);
        }
    }
    if (text == QLatin1String("-0")) text = QStringLiteral("0");
    return text.replace(u'.', decimalPoint(format));
}

Checked check(const QString &input, const Format &format, const std::optional<Range> &range)
{
    const QString text = input.trimmed();
    if (text.isEmpty()) return { Problem::None, QString() };

    const auto inRange = [&range](double v) {
        return !range || ((!range->min || v >= *range->min) && (!range->max || v <= *range->max));
    };

    switch (format.kind) {
    case Format::Kind::Number:
    case Format::Kind::Percent: {
        const auto v = number(text);
        if (!v) return { Problem::NotNumber, input };
        if (!inRange(*v)) return { Problem::OutOfRange, input };
        Format plain = format;
        if (format.kind == Format::Kind::Percent) plain.kind = Format::Kind::None;
        return { Problem::None, numberText(*v, plain) };
    }
    case Format::Kind::Date:
    case Format::Kind::Time: {
        const bool time = format.kind == Format::Kind::Time;
        const auto read = readDate(text, format.pattern, time);
        if (!read) return { time ? Problem::NotTime : Problem::NotDate, input };
        return { Problem::None, QLocale::c().toString(*read, qtPattern(format.pattern)) };
    }
    case Format::Kind::Special: {
        QString digits;
        for (const QChar c : text) if (c.isDigit()) digits += c;
        switch (format.special) {
        case 0: if (digits.size() == 5) return { Problem::None, digits }; break;
        case 1: if (digits.size() == 9) return { Problem::None, digits.left(5) + u'-' + digits.mid(5) }; break;
        case 2:
            if (digits.size() == 10)
                return { Problem::None, QStringLiteral("(%1) %2-%3")
                             .arg(digits.left(3), digits.mid(3, 3), digits.mid(6)) };
            if (digits.size() == 7) return { Problem::None, digits.left(3) + u'-' + digits.mid(3) };
            break;
        case 3:
            if (digits.size() == 9)
                return { Problem::None, QStringLiteral("%1-%2-%3")
                             .arg(digits.left(3), digits.mid(3, 2), digits.mid(5)) };
            break;
        default: return { Problem::None, input };
        }
        return { Problem::NotSpecial, input };
    }
    case Format::Kind::None:
        break;
    }
    if (range) {
        const auto v = number(text);
        if (!v) return { Problem::NotNumber, input };
        if (!inRange(*v)) return { Problem::OutOfRange, input };
    }
    return { Problem::None, input };
}

bool calculates(const QString &script)
{
    return script.contains(QLatin1String("AFSimple_Calculate"))
        || script.contains(QLatin1String("event.value"));
}

std::optional<double> calculate(const QString &script, const Lookup &values)
{
    const QString code = withoutComments(script);
    const auto simple = find(code, kSimple);
    if (simple.hasMatch()) return simpleCalculate(simple, values);

    QHash<QString, double> vars;
    std::optional<double> result;
    for (QString statement : statements(code)) {
        for (const QLatin1String keyword : { QLatin1String("var "), QLatin1String("let "),
                                             QLatin1String("const ") })
            if (statement.startsWith(keyword)) statement = statement.mid(keyword.size());
        const int at = assignment(statement);
        if (at <= 0) return std::nullopt;
        const QString target = statement.left(at).trimmed();
        const auto v = Expression(statement.mid(at + 1), values, vars).parse();
        if (!v || !std::isfinite(*v)) return std::nullopt;
        if (target == QLatin1String("event.value")) result = v;
        else if (target.contains(QRegularExpression(QStringLiteral(R"(^[A-Za-z_$][\w$]*$)")))) vars[target] = *v;
        else return std::nullopt;
    }
    return result;
}

std::optional<Simple> simpleOf(const QString &script)
{
    const auto m = find(withoutComments(script), kSimple);
    if (!m.hasMatch()) return std::nullopt;
    return Simple { m.captured(1).toUpper(), quotedNames(m.captured(2)) };
}

namespace {

QString quoted(const QString &s)
{
    return u'"' + QString(s).replace(u'\\', QLatin1String("\\\\")).replace(u'"', QLatin1String("\\\"")) + u'"';
}

QString formatCall(const Format &format, const QString &kind)
{
    switch (format.kind) {
    case Format::Kind::Number:
        return QStringLiteral("AFNumber_%1(%2, %3, 0, 0, \"\", true);").arg(kind).arg(format.decimals).arg(format.separators);
    case Format::Kind::Percent:
        return QStringLiteral("AFPercent_%1(%2, %3);").arg(kind).arg(format.decimals).arg(format.separators);
    case Format::Kind::Date:
        return QStringLiteral("AFDate_%1Ex(%2);").arg(kind, quoted(format.pattern));
    case Format::Kind::Time:
        return kind == QLatin1String("Format") ? QStringLiteral("AFTime_FormatEx(%1);").arg(quoted(format.pattern))
                                               : QStringLiteral("AFTime_Keystroke(0);");
    case Format::Kind::Special:
        return QStringLiteral("AFSpecial_%1(%2);").arg(kind).arg(format.special);
    case Format::Kind::None:
        break;
    }
    return {};
}

}

QString keystrokeScript(const Format &format) { return formatCall(format, QStringLiteral("Keystroke")); }
QString formatScript(const Format &format)    { return formatCall(format, QStringLiteral("Format")); }

QString rangeScript(const std::optional<Range> &range)
{
    if (!range || (!range->min && !range->max)) return {};
    return QStringLiteral("AFRange_Validate(%1, %2, %3, %4);")
        .arg(range->min ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(QString::number(range->min.value_or(0), 'g', 15))
        .arg(range->max ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(QString::number(range->max.value_or(0), 'g', 15));
}

QString calculationScript(const Simple &simple)
{
    if (simple.operation.isEmpty() || simple.fields.isEmpty()) return {};
    QStringList names;
    for (const QString &f : simple.fields) names << quoted(f);
    return QStringLiteral("AFSimple_Calculate(%1, new Array (%2));")
        .arg(quoted(simple.operation), names.join(QStringLiteral(", ")));
}

}
