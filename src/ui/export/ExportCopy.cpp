#include "ui/export/ExportCopy.hpp"

#include "ui/DocumentView.hpp"
#include "app/PdfPwStore.hpp"

ExportCopy::ExportCopy(DocumentView *view)
{
    if (!view || !m_dir.isValid()) return;
    const QString path = m_dir.filePath(QStringLiteral("export.pdf"));
    if (!view->writeRecoveryCopy(path)) return;
    const QString password = PdfPwStore::get(view->contentFile());
    if (!password.isEmpty()) PdfPwStore::set(path, password);
    m_path = path;
}

ExportCopy::~ExportCopy()
{
#ifdef HAVE_PDF_RENDERING
    m_source.close();
#endif
    if (!m_path.isEmpty()) PdfPwStore::forget(m_path);
}

#ifdef HAVE_PDF_RENDERING
bool ExportCopy::open()
{
    if (m_path.isEmpty()) return false;
    if (m_source.pageCount() > 0) return true;
    if (!m_source.open(m_path, {})) return false;
    m_source.resetContentProvider();
    return m_source.pageCount() > 0;
}

DocumentExporter::Sources ExportCopy::sources() const
{
    DocumentExporter::Sources src;
    src.renderer  = m_source.renderer();
    src.provider  = m_source.contentProvider();
    src.pageCount = m_source.pageCount();
    src.backend   = m_source.backend();
    return src;
}
#endif
