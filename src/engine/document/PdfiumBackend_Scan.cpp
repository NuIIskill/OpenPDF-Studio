#include "engine/document/PdfiumBackend.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumLock.hpp"
#include "engine/document/PdfiumScanText.hpp"

void PdfiumBackend::setScanText(int page, const QList<ScanLine> &lines)
{
    PdfiumLock lock;
    if (!m_doc || page < 0 || page >= pageCount()) return;
    if (!m_scanText) m_scanText = std::make_unique<PdfiumScanText>();
    m_scanText->set(page, pageSizePts(page), lines);
}

bool PdfiumBackend::hasScanText(int page) const
{
    PdfiumLock lock;
    return m_scanText && m_scanText->has(page);
}

// A scan that OCR read is read as the text it holds.
bool PdfiumBackend::readScan(int page, FPDF_PAGE &pg, FPDF_TEXTPAGE &text) const
{
    if (!m_scanText || !m_scanText->has(page) || hasSelectableText(page)) return false;
    pg   = m_scanText->page(page);
    text = m_scanText->text(page);
    return pg && text;
}

QList<PdfBackend::TextMatch> PdfiumBackend::findScanText(int page, const QString &text) const
{
    PdfiumLock lock;
    return m_scanText ? m_scanText->find(page, text) : QList<TextMatch>();
}

#endif
