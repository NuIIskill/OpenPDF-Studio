#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/edit/ContentMap.hpp"
#include "engine/edit/ContentModel.hpp"

#include "fpdfview.h"

class PdfiumContentProvider : public ContentProvider
{
public:

    explicit PdfiumContentProvider(FPDF_DOCUMENT doc);
    ~PdfiumContentProvider() override;

    QList<ContentItem> pageItemsForExport(int page) override;

protected:
    QList<ContentItem> buildPage(int page) override;

private:
    QList<ContentItem> buildPageItems(int page, bool mergeVertical);

    QList<ContentCluster> collectWords(FPDF_TEXTPAGE tp, double pageHeight) const;

    void collectAnnotations(FPDF_PAGE pg, double pageHeight,
                            QList<ContentItem> *fields,
                            QList<ContentItem> *media) const;

    FPDF_DOCUMENT m_doc { nullptr };
};

#endif
