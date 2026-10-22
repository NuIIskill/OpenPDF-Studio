#include "engine/edit/TextGutters.hpp"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

bool TextGutters::separates(const QList<Row> &rows, int row, double gapLeft, double gapRight)
{
    if (row < 0 || row >= rows.size()) return false;
    const Row &here = rows.at(row);
    const double em = qMax(here.size, 4.0);
    if (gapRight - gapLeft < qMax(4.0, em)) return false;
    // Justified lines end up to a side bearing apart, so the channel keeps some margin.
    const double margin = qMax(1.0, em * 0.15);
    const double left = gapLeft + margin;
    const double right = gapRight - margin;

    // The channel runs on through a neighbouring row where enough of it stays
    // free of ink, and counts where text lines it there, as along a column or
    // a cell; a short cell beside it lets it pass. A wide space in justified
    // text has words above and below it instead.
    const double narrowest = qMax(4.0, em * 0.6) - 2.0 * margin;
    const auto lined = [&](int direction) {
        double from = left, to = right;
        int count = 0;
        for (int k = row + direction, seen = 0; k >= 0 && k < rows.size() && seen < 2;
             k += direction, ++seen) {
            if (std::abs(rows.at(k).baseline - here.baseline) > em * 5.0) break;
            // The widest stretch of the channel this row leaves free.
            double freeFrom = from, bestFrom = from, bestTo = from;
            bool borders = false;
            QList<QPair<double, double>> ink = rows.at(k).ink;
            std::sort(ink.begin(), ink.end());
            for (const auto &[l, r] : std::as_const(ink)) {
                if ((r <= from && r >= from - em * 1.5) || (l >= to && l <= to + em * 1.5))
                    borders = true;
                if (r <= freeFrom || l >= to) continue;
                if (l - freeFrom > bestTo - bestFrom) { bestFrom = freeFrom; bestTo = l; }
                freeFrom = qMax(freeFrom, r);
                borders = true;
            }
            if (to - freeFrom > bestTo - bestFrom) { bestFrom = freeFrom; bestTo = to; }
            if (bestTo - bestFrom < narrowest) break;
            from = bestFrom;
            to = bestTo;
            if (borders) ++count;
        }
        return count;
    };
    const int above = lined(-1);
    const int below = lined(1);
    return (above >= 1 && below >= 1) || above >= 2 || below >= 2;
}
