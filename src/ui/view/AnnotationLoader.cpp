#include "ui/view/AnnotationLoader.hpp"

#include "ui/notes/NoteLayer.hpp"
#include "ui/view/LinkAnnotationLayer.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentWorker.hpp"
#  include "engine/document/PdfBackend.hpp"
#endif

#include <QElapsedTimer>
#include <QTimer>

AnnotationLoader::AnnotationLoader(LinkAnnotationLayer *links, NoteLayer *notes,
                                   QObject *parent)
    : QObject(parent)
    , m_links(links)
    , m_notes(notes)
{
    m_links->setBeforeChange([this] { finish(); });
    m_notes->setBeforeChange([this] { finish(); });
}

void AnnotationLoader::setSource(PdfBackend *backend, DocumentWorker *worker)
{
    m_backend = backend;
    m_worker  = worker;
#ifdef HAVE_PDF_RENDERING
    if (!m_worker) return;
    connect(m_worker, &DocumentWorker::annotationsRead, this,
            [this](int page, const QList<PdfBackend::Link> &links,
                   const QList<PdfBackend::Note> &notes) {
        if (page < 0 || page >= m_loaded.size() || m_loaded.testBit(page)) return;
        m_loaded.setBit(page);
        --m_remaining;
        m_links->addPage(page, links);
        m_notes->addPage(page, notes);
    });
    // A stop happens inside an open; resuming waits until the open is done.
    connect(m_worker, &DocumentWorker::stopped, this, [this] {
        QTimer::singleShot(0, this, &AnnotationLoader::resume);
    });
#endif
}

void AnnotationLoader::start(int pageCount)
{
    constexpr int kStartBudgetMs = 50;
    m_links->clear();
    m_notes->clear();
    m_loaded    = QBitArray(qMax(0, pageCount));
    m_remaining = qMax(0, pageCount);
    // Small documents are complete when the open returns, as before; only
    // what does not fit the budget is left to the worker.
    readHere(kStartBudgetMs);
    resume();
}

void AnnotationLoader::finish()
{
    if (m_remaining <= 0) return;
#ifdef HAVE_PDF_RENDERING
    if (m_worker) m_worker->readAnnotations({});
#endif
    readHere(-1);
}

void AnnotationLoader::readHere(int budgetMs)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_backend) return;
    QElapsedTimer clock;
    clock.start();
    for (int page : remainingPages()) {
        if (budgetMs >= 0 && clock.elapsed() >= budgetMs) break;
        m_loaded.setBit(page);
        --m_remaining;
        m_links->addPage(page, m_backend->pageLinks(page));
        m_notes->addPage(page, m_backend->pageNotes(page));
    }
#else
    Q_UNUSED(budgetMs)
#endif
}

QList<int> AnnotationLoader::remainingPages() const
{
    QList<int> pages;
    for (int page = 0; page < m_loaded.size(); ++page)
        if (!m_loaded.testBit(page)) pages.append(page);
    return pages;
}

void AnnotationLoader::resume()
{
#ifdef HAVE_PDF_RENDERING
    if (m_worker) m_worker->readAnnotations(m_remaining > 0 ? remainingPages() : QList<int>());
#endif
}
