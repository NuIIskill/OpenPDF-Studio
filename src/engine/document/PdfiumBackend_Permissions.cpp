#include "engine/document/PdfiumBackend.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumLock.hpp"

namespace {

// Print, modify, copy, annotate, forms, accessibility, assemble, high-quality print.
constexpr unsigned long kPermissionBits = 0xf3cUL;

DocumentInfo::Permissions decode(unsigned long flags, int revision)
{
    const auto bit = [flags](int n) { return (flags & (1UL << (n - 1))) != 0; };
    const bool extended = revision >= 3;
    DocumentInfo::Permissions p;
    p.print         = bit(3);
    p.printHighRes  = bit(3) && (!extended || bit(12));
    p.modify        = bit(4);
    p.copy          = bit(5);
    p.annotate      = bit(6);
    p.fillForms     = bit(6) || (extended && bit(9));
    p.assemble      = extended ? bit(11) : bit(4);
    p.accessibility = extended ? bit(10) : bit(5);
    return p;
}

}

DocumentInfo::Permissions PdfiumBackend::permissions() const
{
    PdfiumLock lock;
    if (!m_doc) return {};
    return decode(FPDF_GetDocPermissions(m_doc), FPDF_GetSecurityHandlerRevision(m_doc));
}

bool PdfiumBackend::isOwnerPassword(const QString &file, const QString &password) const
{
    if (file.isEmpty() || password.isEmpty()) return false;
    PdfiumLock lock;
    const QByteArray pw = password.toUtf8();
    FPDF_DOCUMENT doc = FPDF_LoadDocument(file.toUtf8().constData(), pw.constData());
    if (!doc) return false;
    // Owner access sets every permission bit, whatever the file restricts.
    const bool owner = (FPDF_GetDocPermissions(doc) & kPermissionBits) == kPermissionBits;
    FPDF_CloseDocument(doc);
    return owner;
}

#endif
