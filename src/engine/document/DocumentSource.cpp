#include "engine/document/DocumentSource.hpp"

#ifdef HAVE_PDF_RENDERING
#endif

DocumentSource::DocumentSource()
{
#ifdef HAVE_PDF_RENDERING
    m_backend = PdfBackend::create();

    m_renderer = new PdfRenderer(m_backend.get());
#endif
}

DocumentSource::~DocumentSource()
{
#ifdef HAVE_PDF_RENDERING

    m_contentProvider.reset();
    delete m_renderer;
    m_backend.reset();
#endif
}

#ifdef HAVE_PDF_RENDERING

bool DocumentSource::open(const QString &path, const PasswordAsker &ask)
{

    m_contentProvider.reset();
    if (!m_backend || !m_backend->open(path, ask)) return false;
    m_contentPath = path;
    m_pageCount   = m_backend->pageCount();

    return true;
}

void DocumentSource::close()
{

    m_contentProvider.reset();
    if (m_backend) m_backend->close();
    m_contentPath.clear();
    m_pageCount = 0;
}

void DocumentSource::resetContentProvider()
{
    m_contentProvider.reset();
    if (m_backend) m_contentProvider = m_backend->makeContentProvider();
}

#endif
