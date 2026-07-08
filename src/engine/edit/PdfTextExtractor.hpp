#pragma once

#include "TextBlock.hpp"
#include <QPointF>
#include <QSizeF>
#include <QList>

#ifdef HAVE_QT_PDF
#include <QPdfDocument>

class PdfTextExtractor
{
public:
    explicit PdfTextExtractor(QPdfDocument *doc);

    // Find the text block (line) the user clicked on.
    // pdfPt is in PDF-point view coordinates (top-left origin, Y down).
    // pageSizePts is the page's full size in PDF points.
    // Returns an invalid TextBlock if no text found at that position.
    TextBlock textAt(int page, const QPointF &pdfPt, const QSizeF &pageSizePts) const;

    // All text blocks on a page (for diagnostics / future use).
    QList<TextBlock> allBlocks(int page) const;

    // Text inside a rectangle (PDF-point coords, Y=0 at top), lines separated
    // by '\n'. Used to fetch the full text of a detected paragraph block.
    QString textInRect(int page, const QRectF &rect) const;

private:
    TextBlock fetchBlock(int page, const QRectF &r, const QSizeF &pageSizePts) const;

    QPdfDocument *m_doc;
};

#endif // HAVE_QT_PDF
