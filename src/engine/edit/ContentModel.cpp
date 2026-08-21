#include "engine/edit/ContentModel.hpp"

#include <QDebug>
#include <QElapsedTimer>

// ── ContentProvider (cache) ───────────────────────────────────────────────────

QList<ContentItem> ContentProvider::pageItems(int page)
{
    auto it = m_cache.find(page);
    if (it == m_cache.end()) {
        QElapsedTimer timer;
        timer.start();
        it = m_cache.insert(page, buildPage(page));
        qDebug() << "[ContentModel] built page" << page << "items=" << it->size()
                 << "in" << timer.elapsed() << "ms";
    }
    return *it;
}

ContentItem ContentProvider::itemAt(int page, const QPointF &pdfPt,
                                    unsigned typeMask, double maxDistance)
{
    return contentItemAt(pageItems(page), pdfPt, typeMask,
                         maxDistance < 0.0 ? 0.0 : maxDistance);
}
