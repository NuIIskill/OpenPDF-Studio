#pragma once

#include <QRectF>
#include <QString>

// Represents one text run on a PDF page (a line or a word block).
// pdfBounds is in PDF-point coordinates with top-left origin (Qt convention).
struct TextBlock {
    int     page     { -1 };
    QRectF  pdfBounds;
    QString text;

    bool isValid() const { return page >= 0 && !pdfBounds.isEmpty(); }
};
