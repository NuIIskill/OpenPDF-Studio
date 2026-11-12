#include "engine/document/PdfiumBackend.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "app/PdfPwStore.hpp"
#include "engine/document/PdfiumEdits.hpp"
#include "engine/document/PdfiumFonts.hpp"
#include "engine/document/PdfiumForms.hpp"
#include "engine/document/PdfiumLock.hpp"
#include "engine/forms/FormFieldWriter.hpp"

#include "fpdf_formfill.h"

#include <algorithm>

bool PdfiumBackend::hasForms() const
{
    PdfiumLock lock;
    if (!m_doc) return false;
    const int type = FPDF_GetFormType(m_doc);
    return type == FORMTYPE_ACRO_FORM || type == FORMTYPE_XFA_FOREGROUND;
}

QList<PdfBackend::FormWidget> PdfiumBackend::pageForms(int page) const
{
    PdfiumLock lock;
    if (!m_doc || page < 0 || page >= pageCount()) return {};
    // What the file has, not what filling in has made of it.
    if (!m_formApplied.isEmpty() || !m_formDesigned.isEmpty()) dropFormDoc();
    if (!syncForms({}, {})) return {};
    return m_forms->read(page);
}

bool PdfiumBackend::syncForms(const QList<EditSession::FormValue> &values,
                              const QList<FormSpec> &design) const
{
    if (m_formDoc && values == m_formApplied && design == m_formDesigned) return true;

    // A field set back needs a fresh load: its old appearance is gone from this one.
    const bool onlyChanged = m_formDoc
        && std::all_of(m_formApplied.cbegin(), m_formApplied.cend(),
                       [&values](const EditSession::FormValue &applied) {
            return std::any_of(values.cbegin(), values.cend(),
                               [&applied](const EditSession::FormValue &v) {
                return v.field == applied.field;
            });
        });
    if (!onlyChanged || design != m_formDesigned) {
        dropFormDoc();
        const QByteArray pw = PdfPwStore::get(m_path).toUtf8();
        // A designed form is shown from the same document the save starts from.
        if (!design.isEmpty()) {
            m_formBytes = FormFieldWriter::build(m_path, PdfPwStore::forQpdf(m_path), design);
            if (m_formBytes.isEmpty()) return false;
            m_formDoc = FPDF_LoadMemDocument64(m_formBytes.constData(), size_t(m_formBytes.size()),
                                               pw.isEmpty() ? nullptr : pw.constData());
        } else {
            const QByteArray path = m_path.toUtf8();
            m_formDoc = FPDF_LoadDocument(path.constData(), pw.isEmpty() ? nullptr : pw.constData());
        }
        if (!m_formDoc) return false;
        m_forms = std::make_unique<PdfiumForms>(m_formDoc);
        m_formDesigned = design;
    }

    QList<EditSession::FormValue> changed;
    for (const EditSession::FormValue &v : values)
        if (!m_formApplied.contains(v)) changed.append(v);
    m_forms->apply(changed);
    m_formApplied = values;
    return true;
}

QImage PdfiumBackend::renderFormPage(int page, const QSize &px, const QRect &target,
                                     const EditSession &session) const
{
    if (!syncForms(session.formValues(), session.formDesign())) return {};
    FPDF_PAGE pg = FPDF_LoadPage(m_formDoc, page);
    if (!pg) return {};
    PdfiumEdits::applyToPage(m_formDoc, pg, page, session);
    PdfiumEdits::showNoteHighlights(pg, page, &session);

    QImage rendered;
    if (FPDF_BITMAP bmp = FPDFBitmap_Create(target.width(), target.height(), 0)) {
        FPDFBitmap_FillRect(bmp, 0, 0, target.width(), target.height(), 0xFFFFFFFF);
        FPDF_RenderPageBitmap(bmp, pg, -target.x(), -target.y(), px.width(), px.height(),
                              0, FPDF_ANNOT);
        FPDF_FFLDraw(m_forms->handle(), bmp, pg, -target.x(), -target.y(), px.width(), px.height(),
                     0, FPDF_ANNOT);
        if (const auto *buffer = static_cast<const uchar *>(FPDFBitmap_GetBuffer(bmp)))
            rendered = QImage(buffer, target.width(), target.height(),
                              FPDFBitmap_GetStride(bmp), QImage::Format_RGB32).copy();
        FPDFBitmap_Destroy(bmp);
    }
    if (m_forms->valid()) FORM_OnBeforeClosePage(pg, m_forms->handle());
    FPDF_ClosePage(pg);
    return rendered;
}

void PdfiumBackend::drawForms(FPDF_BITMAP bmp, FPDF_PAGE pg, const QRect &target,
                              const QSize &px) const
{
    if (m_docForms && m_docForms->valid())
        FPDF_FFLDraw(m_docForms->handle(), bmp, pg, -target.x(), -target.y(), px.width(),
                     px.height(), 0, FPDF_ANNOT);
}

void PdfiumBackend::closeShownPage(FPDF_PAGE pg) const
{
    if (m_docForms && m_docForms->valid()) FORM_OnBeforeClosePage(pg, m_docForms->handle());
    FPDF_ClosePage(pg);
}

void PdfiumBackend::dropFormDoc() const
{
    m_forms.reset();
    if (m_formDoc) {
        PdfiumFonts::releaseFonts(m_formDoc);
        FPDF_CloseDocument(m_formDoc);
        m_formDoc = nullptr;
    }
    m_formBytes.clear();
    m_formApplied.clear();
    m_formDesigned.clear();
}

#endif
