#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include <QRectF>
#include <QtGlobal>

namespace PdfiumTextRules {

inline bool separatesWords(const QRectF &prev, const QRectF &next, double fontSize)
{
    const double gap = next.left() - prev.right();
    return gap > qMax(2.0, fontSize * 0.5);
}

inline bool separatesBlocks(const QRectF &prev, const QRectF &next, double fontSize)
{
    const double gap = next.left() - prev.right();
    return gap > qMax(12.0, fontSize * 2.5);
}

inline bool sameGlyph(const QRectF &a, const QRectF &b)
{
    const QRectF hit = a.intersected(b);
    if (hit.isEmpty()) return false;
    const double areaA = a.width() * a.height();
    const double areaB = b.width() * b.height();
    const double small = qMax(1e-6, qMin(areaA, areaB));
    return hit.width() * hit.height() / small > 0.7;
}

inline bool sameLine(double baselineA, double baselineB, double charHeight)
{
    return qAbs(baselineA - baselineB) <= qMax(2.0, charHeight * 0.5);
}

}

#endif
