#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfBackend.hpp"
#include "engine/edit/EditSession.hpp"

#include "fpdf_formfill.h"
#include "fpdfview.h"

#include <QList>

/// Reads and fills the interactive form of one PDFium document the way a viewer filling it in would.
class PdfiumForms
{
public:
    explicit PdfiumForms(FPDF_DOCUMENT doc);
    ~PdfiumForms();

    PdfiumForms(const PdfiumForms &)            = delete;
    PdfiumForms &operator=(const PdfiumForms &) = delete;

    bool valid() const { return m_form != nullptr; }
    FPDF_FORMHANDLE handle() const { return m_form; }

    QList<PdfBackend::FormWidget> read(int page) const;

    void apply(const QList<EditSession::FormValue> &values);

private:
    FPDF_DOCUMENT     m_doc  { nullptr };
    FPDF_FORMFILLINFO m_info {};
    FPDF_FORMHANDLE   m_form { nullptr };
};

#endif
