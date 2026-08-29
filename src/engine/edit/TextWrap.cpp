#include "engine/edit/TextWrap.hpp"

#include <QList>

namespace {

QStringList wrapOne(const QString &line, double maxWidth,
                    const std::function<double(QChar)> &advance,
                    double charSpacing)
{
    QList<double> bis;
    bis.reserve(line.size() + 1);
    double breite = 0.0;
    bis.append(0.0);
    for (int i = 0; i < line.size(); ++i) {
        breite += advance(line.at(i));
        if (i > 0) breite += charSpacing;
        bis.append(breite);
    }
    if (bis.constLast() <= maxWidth) return { line };

    QStringList out;
    int start = 0;
    while (start < line.size()) {
        int ende = start;
        while (ende < line.size() && bis.at(ende + 1) - bis.at(start) <= maxWidth)
            ++ende;
        if (ende <= start) ende = start + 1;
        if (ende < line.size()) {
            int leer = -1;
            for (int i = ende - 1; i > start; --i)
                if (line.at(i).isSpace()) { leer = i; break; }
            if (leer > start) {
                out.append(line.mid(start, leer - start));
                start = leer + 1;
                continue;
            }
        }
        out.append(line.mid(start, ende - start));
        start = ende;
    }
    return out;
}

}

QStringList TextWrap::lines(const QString &text, double maxWidth,
                            const std::function<double(QChar)> &advance,
                            double charSpacing)
{
    if (maxWidth <= 0.0 || !advance) return text.split(QLatin1Char('\n'));
    QStringList out;
    for (const QString &line : text.split(QLatin1Char('\n')))
        out.append(line.isEmpty() ? QStringList{ line }
                                  : wrapOne(line, maxWidth, advance, charSpacing));
    return out;
}
