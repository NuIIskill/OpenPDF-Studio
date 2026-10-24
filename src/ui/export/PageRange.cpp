#include "ui/export/PageRange.hpp"

#include <QStringList>

#include <utility>

namespace PageRange {

QList<int> parse(const QString &text, int pageCount, bool *ok)
{
    if (ok) *ok = false;
    QList<int> pages;
    const QStringList parts = text.trimmed().split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &rawPart : parts) {
        const QString part = rawPart.trimmed();
        if (part.isEmpty()) continue;
        bool okFrom = false, okTo = false;
        int from = 0, to = 0;
        const int dash = part.indexOf(QLatin1Char('-'));
        if (dash < 0) {
            from = to = part.toInt(&okFrom);
            okTo = okFrom;
        } else {
            from = part.left(dash).trimmed().toInt(&okFrom);
            to   = part.mid(dash + 1).trimmed().toInt(&okTo);
        }
        if (!okFrom || !okTo) return {};
        if (from > to) std::swap(from, to);
        if (from < 1 || to > pageCount) return {};
        for (int p = from; p <= to; ++p)
            if (!pages.contains(p - 1)) pages.append(p - 1);
    }
    if (pages.isEmpty()) return pages;
    if (ok) *ok = true;
    return pages;
}

}
