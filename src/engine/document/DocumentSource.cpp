#include "engine/document/DocumentSource.hpp"

#ifdef HAVE_PDF_RENDERING
#endif

DocumentSource::DocumentSource()
{
#ifdef HAVE_PDF_RENDERING
    m_backend = PdfBackend::create();
    // One renderer for the life of the source: it borrows the backend, and the
    // backend swaps documents behind it.
    m_renderer = new PdfRenderer(m_backend.get());
#endif
}

DocumentSource::~DocumentSource()
{
#ifdef HAVE_PDF_RENDERING
    // The provider references the backend's document, so it has to go first —
    // explicitly, not by member declaration order, which is too easy to
    // reorder by accident.
    m_contentProvider.reset();
    delete m_renderer;
    m_backend.reset();
#endif
}

#ifdef HAVE_PDF_RENDERING

bool DocumentSource::open(const QString &path, const PasswordAsker &ask)
{
    // Das alte Modell beschreibt eine Datei, die gleich nicht mehr offen ist —
    // und es hält auf dem Poppler-Pfad einen Zeiger in deren Dokument.
    m_contentProvider.reset();
    if (!m_backend || !m_backend->open(path, ask)) return false;
    m_contentPath = path;
    m_pageCount   = m_backend->pageCount();
    // Aufgebaut wird das neue Modell nicht hier: der Aufrufer verdrahtet damit
    // auch Hover und Auswahl und ruft dafür resetContentProvider().
    return true;
}

void DocumentSource::close()
{
    // Provider first: it reads from the document that is about to go away, and
    // on the Poppler path it holds a raw pointer into it.
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

#endif // HAVE_PDF_RENDERING
