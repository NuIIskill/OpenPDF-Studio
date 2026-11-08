#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfBackend.hpp"

#include "fpdf_text.h"
#include "fpdfview.h"

#include <QHash>
#include <QList>
#include <QRawFont>
#include <QRectF>
#include <QSizeF>
#include <QString>

/// What OCR read on scanned pages, set as text of a document kept in memory so that the normal search reads it.
class PdfiumScanText
{
public:
    PdfiumScanText();
    ~PdfiumScanText();

    PdfiumScanText(const PdfiumScanText &)            = delete;
    PdfiumScanText &operator=(const PdfiumScanText &) = delete;

    bool has(int page) const { return m_pages.contains(page); }
    void set(int page, const QSizeF &pageSize, const QList<PdfBackend::ScanLine> &lines);
    QList<PdfBackend::TextMatch> find(int page, const QString &text) const;
    FPDF_PAGE     page(int page) const { return m_pages.value(page).page; }
    FPDF_TEXTPAGE text(int page) const { return m_pages.value(page).text; }
    QList<QRectF> boxes(int page) const { return m_pages.value(page).boxes; }

private:
    struct Page {
        FPDF_PAGE     page { nullptr };
        FPDF_TEXTPAGE text { nullptr };
        QList<QRectF> boxes;
        QList<int>    lines;
    };

    FPDF_FONT fontFor(const QString &text);

    FPDF_DOCUMENT           m_doc { nullptr };
    QHash<int, Page>        m_pages;
    QHash<QString, QRawFont> m_faces;
};

#endif
