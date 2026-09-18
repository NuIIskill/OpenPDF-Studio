#pragma once

#include "engine/edit/ContentMap.hpp"

#include <QHash>
#include <QSizeF>
#include <functional>

/// Framework entry point for page-content detection.
class ContentProvider
{
public:
    virtual ~ContentProvider() = default;

    QList<ContentItem> pageItems(int page);

    virtual QList<ContentItem> pageItemsForExport(int page)
    {
        return pageItems(page);
    }

    bool hasPage(int page) const { return m_cache.contains(page); }

    ContentItem itemAt(int page, const QPointF &pdfPt,
                       unsigned typeMask = kTextualContentTypes,
                       double maxDistance = 40.0);

    void invalidatePage(int page) { m_cache.remove(page); }
    void invalidateAll()          { m_cache.clear(); }

protected:
    virtual QList<ContentItem> buildPage(int page) = 0;

private:
    QHash<int, QList<ContentItem>> m_cache;
};
