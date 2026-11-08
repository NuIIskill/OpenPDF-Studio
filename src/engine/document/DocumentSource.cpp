#include "engine/document/DocumentSource.hpp"

DocumentSource::DocumentSource()
{
#ifdef HAVE_PDF_RENDERING
    m_backend = PdfBackend::create();

    m_renderer = new PdfRenderer(m_backend.get());
    m_worker   = std::make_unique<DocumentWorker>(m_backend.get());
    // A scan read since its content was found is found again, now with its text.
    QObject::connect(m_worker.get(), &DocumentWorker::scanTextRead, m_worker.get(), [this](int page) {
        if (m_contentProvider) m_contentProvider->invalidatePage(page);
    });
#endif
}

DocumentSource::~DocumentSource()
{
#ifdef HAVE_PDF_RENDERING

    m_worker.reset();
    m_contentProvider.reset();
    delete m_renderer;
    m_backend.reset();
#endif
}

#ifdef HAVE_PDF_RENDERING

bool DocumentSource::open(const QString &path, const PasswordAsker &ask)
{
    m_worker->stop();
    m_contentProvider.reset();
    if (!m_backend || !m_backend->open(path, ask)) return false;
    m_contentPath = path;
    m_pageCount   = m_backend->pageCount();
    m_worker->setDocument(path);

    return true;
}

void DocumentSource::close()
{
    m_worker->stop();
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
