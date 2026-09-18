#pragma once

#include <QRectF>
#include <QString>

/// Represents one text run on a PDF page (a line or a word block).
struct TextBlock {
    int     page     { -1 };
    QRectF  pdfBounds;
    QString text;

    bool isValid() const { return page >= 0 && !pdfBounds.isEmpty(); }
};
