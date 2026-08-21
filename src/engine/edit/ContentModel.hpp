#pragma once

#include "engine/edit/ContentMap.hpp"

#include <QHash>
#include <QSizeF>
#include <functional>

// ── ContentProvider ───────────────────────────────────────────────────────────
// Framework entry point for page-content detection. DocumentView owns one
// provider per open file; results are built lazily and cached per page, so
// clicks and hover lookups after the first are O(items) with no file I/O.
//
// Extension points:
//   • new backends: subclass and implement buildPage() (see the two below)
//   • new region types: extend ContentItem::Type — providers that can detect
//     the type emit it, consumers filter via the typeMask of itemAt()
class ContentProvider
{
public:
    virtual ~ContentProvider() = default;

    // Detected items for a page; builds once, then serves from cache.
    // Returned by value on purpose: QList is implicitly shared (O(1) copy)
    // and a reference into the cache would dangle across invalidate calls.
    QList<ContentItem> pageItems(int page);

    // True when the page has already been built. Cheap callers (hover!) must
    // check this and NEVER trigger a build — buildPage parses the whole file
    // on the UI thread and freezes scrolling on complex documents.
    bool hasPage(int page) const { return m_cache.contains(page); }

    // Spatial lookup (see contentItemAt). maxDistance < 0 → exact hits only.
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
