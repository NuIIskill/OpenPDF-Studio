#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include <QRectF>

#include <fpdf_edit.h>
#include <fpdf_text.h>

#include <cmath>
#include <QtGlobal>

namespace PdfiumTextRules {

/// The font size PDFium reports for a character is the one written in the
/// content stream, without the text object's own matrix. Most writers leave
/// that matrix at unity, but Qt's PDF engine scales everything through it: a
/// 9 pt line is written as 150 pt with a matrix of 0.06. Uncorrected the size
/// is out by more than a factor of ten, no word gap ever counts as a word
/// break, and the whole page collapses into a single run.
inline double effectiveFontSize(FPDF_TEXTPAGE page, int index)
{
    const double reported = FPDFText_GetFontSize(page, index);
    FPDF_PAGEOBJECT object = FPDFText_GetTextObject(page, index);
    FS_MATRIX matrix;
    if (!object || !FPDFPageObj_GetMatrix(object, &matrix)) return reported;

    const double determinant = double(matrix.a) * matrix.d - double(matrix.b) * matrix.c;
    const double scale = std::sqrt(std::fabs(determinant));
    return scale > 0.0 ? reported * scale : reported;
}

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
