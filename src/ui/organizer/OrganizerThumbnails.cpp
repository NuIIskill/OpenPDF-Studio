#include "ui/organizer/OrganizerThumbnails.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/document/DocumentWorker.hpp"
#include "engine/document/OrganizerDoc.hpp"

OrganizerThumbnails::OrganizerThumbnails(const QMap<QString, OrganizerDoc *> &docs,
                                         QObject *parent)
    : QObject(parent)
    , m_docs(docs)
{
}

QImage OrganizerThumbnails::image(const QString &pdfPath, int pageIndex) const
{
    const auto doc = m_images.constFind(pdfPath);
    return doc == m_images.cend() ? QImage() : doc->value(pageIndex);
}

void OrganizerThumbnails::request(const QList<Page> &pages, const QSize &box, qreal dpr)
{
    QHash<QString, QList<DocumentWorker::Render>> renders;
    QHash<QString, QSet<int>> queued;
    for (const Page &page : pages) {
        OrganizerDoc *doc = m_docs.value(page.pdfPath);
        if (!doc || !image(page.pdfPath, page.pageIndex).isNull()) continue;
        QSet<int> &pending = queued[page.pdfPath];
        if (pending.contains(page.pageIndex)) continue;
        const QSizeF pts = doc->pageSizePts(page.pageIndex);
        if (pts.isEmpty()) continue;
        pending.insert(page.pageIndex);
        DocumentWorker::Render render;
        render.page  = page.pageIndex;
        render.scale = qMin(box.width() / pts.width(), box.height() / pts.height()) * dpr;
        renders[page.pdfPath].append(render);
    }
    for (auto it = m_docs.cbegin(); it != m_docs.cend(); ++it) {
        const QList<DocumentWorker::Render> wanted = renders.value(it.key());
        if (wanted.isEmpty() && !m_listening.contains(it.key())) continue;
        listen(it.key(), it.value());
        it.value()->worker()->setRenders(wanted);
    }
}

void OrganizerThumbnails::listen(const QString &pdfPath, OrganizerDoc *doc)
{
    if (m_listening.contains(pdfPath)) return;
    m_listening.insert(pdfPath);
    connect(doc->worker(), &DocumentWorker::pageRendered, this,
            [this, pdfPath](int page, int, int, const QRect &, const QImage &image, int) {
        m_images[pdfPath].insert(page, image);
        Q_EMIT rendered(pdfPath, page);
    });
}

#endif
