#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/edit/ContentMap.hpp"
#include "engine/edit/ContentModel.hpp"

#include "fpdfview.h"

#include <QMap>

#include <functional>

class PdfiumPageSpace;

class PdfiumContentProvider : public ContentProvider
{
public:

    using ScanText = std::function<bool(int page, FPDF_PAGE &pg, FPDF_TEXTPAGE &text, QList<QRectF> &boxes)>;

    explicit PdfiumContentProvider(FPDF_DOCUMENT doc, ScanText scanText = {});
    ~PdfiumContentProvider() override;

    QList<ContentItem> pageItemsForExport(int page) override;

protected:
    QList<ContentItem> buildPage(int page) override;

private:
    QList<ContentItem> buildPageItems(int page, bool mergeVertical);

    QMap<int, QList<ContentCluster>> collectWords(FPDF_TEXTPAGE tp, const PdfiumPageSpace &space,
                                                  bool byTurn, const QList<QRectF> &boxes = {}) const;

    void collectAnnotations(FPDF_PAGE pg, const PdfiumPageSpace &space,
                            QList<ContentItem> *fields,
                            QList<ContentItem> *media) const;

    FPDF_DOCUMENT m_doc { nullptr };
    ScanText      m_scanText;
};

#endif
