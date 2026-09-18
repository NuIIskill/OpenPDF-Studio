#pragma once

#include <QColor>
#include <QStringView>
#include <QXmlStreamReader>

/// Small shared helpers for the two office formats.
///
/// Attributes and elements are matched by local name only. Word writes the same
/// document under two different namespace URIs depending on how strictly it was
/// saved, and nothing here needs to tell those apart.
namespace XmlRead {

inline QStringView attribute(const QXmlStreamReader &xml, QStringView name)
{
    for (const QXmlStreamAttribute &a : xml.attributes())
        if (a.name() == name) return a.value();
    return {};
}

inline bool hasAttribute(const QXmlStreamReader &xml, QStringView name)
{
    return !attribute(xml, name).isNull();
}

/// A w:val that is absent means on, "0", "false" and "none" mean off.
inline bool onOff(QStringView value)
{
    return value.isNull()
        || (value != QStringView(u"0") && value != QStringView(u"false")
            && value != QStringView(u"none") && value != QStringView(u"off"));
}

inline double twipsToPt(QStringView value)
{
    bool ok = false;
    const double twips = value.toDouble(&ok);
    return ok ? twips / 20.0 : 0.0;
}

/// English Metric Units, the unit inside a DrawingML drawing.
inline double emuToPt(QStringView value)
{
    bool ok = false;
    const double emu = value.toDouble(&ok);
    return ok ? emu / 12700.0 : 0.0;
}

/// A border width, written in eighths of a point.
inline double eighthPointsToPt(QStringView value)
{
    bool ok = false;
    const double eighths = value.toDouble(&ok);
    return ok ? eighths / 8.0 : 0.0;
}

inline QColor hexColor(QStringView value)
{
    if (value.isEmpty() || value == QStringView(u"auto")) return {};
    QString text = value.toString();
    if (!text.startsWith(QLatin1Char('#'))) text.prepend(QLatin1Char('#'));
    const QColor color(text);
    return color.isValid() ? color : QColor();
}

/// An ODF length such as "2.54cm", "1in", "12pt" or "0.5in".
inline double odfLengthToPt(QStringView value)
{
    if (value.isEmpty()) return 0.0;

    int digits = 0;
    while (digits < value.size()
           && (value.at(digits).isDigit() || value.at(digits) == u'.'
               || value.at(digits) == u'-' || value.at(digits) == u'+'))
        ++digits;

    bool ok = false;
    const double number = value.left(digits).toDouble(&ok);
    if (!ok) return 0.0;

    const QStringView unit = value.mid(digits).trimmed();
    if (unit == QStringView(u"cm")) return number * 72.0 / 2.54;
    if (unit == QStringView(u"mm")) return number * 72.0 / 25.4;
    if (unit == QStringView(u"in")) return number * 72.0;
    if (unit == QStringView(u"pc")) return number * 12.0;
    if (unit == QStringView(u"px")) return number * 72.0 / 96.0;
    return number;   // pt, and anything unitless
}

/// An ODF border such as "0.5pt solid #bfbfbf", or "none".
inline bool odfBorder(QStringView value, double *widthPt, QColor *color)
{
    if (value.isEmpty() || value == QStringView(u"none")) return false;

    for (const QStringView part : value.tokenize(u' ', Qt::SkipEmptyParts)) {
        if (part.startsWith(u'#')) {
            const QColor found(part.toString());
            if (found.isValid()) *color = found;
        } else if (part.at(0).isDigit() || part.at(0) == u'.') {
            *widthPt = odfLengthToPt(part);
        }
    }
    return true;
}

}
