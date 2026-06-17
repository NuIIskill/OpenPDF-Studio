#pragma once

#include "TextBlock.hpp"
#include <QPointF>
#include <QSizeF>
#include <QList>

#ifdef HAVE_PDF_RENDERING

#ifdef HAVE_QT_PDF
#  include <QPdfDocument>
#elif defined(HAVE_POPPLER)
#  include <poppler/qt6/poppler-qt6.h>
#endif

class PdfTextExtractor
{
public:
#ifdef HAVE_QT_PDF
    explicit PdfTextExtractor(QPdfDocument *doc);
#else
    explicit PdfTextExtractor(Poppler::Document *doc);
#endif

    TextBlock textAt(int page, const QPointF &pdfPt, const QSizeF &pageSizePts) const;
    QList<TextBlock> allBlocks(int page) const;

private:
#ifdef HAVE_QT_PDF
    TextBlock fetchBlock(int page, const QRectF &r, const QSizeF &pageSizePts) const;
    QPdfDocument *m_doc;
#else
    QList<TextBlock> buildBlocks(int page) const;
    Poppler::Document *m_doc;
#endif
};

#endif // HAVE_PDF_RENDERING
