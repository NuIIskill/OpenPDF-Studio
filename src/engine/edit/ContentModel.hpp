#pragma once

#include "ContentMap.hpp"

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

// ── qpdf backend ──────────────────────────────────────────────────────────────
#ifdef HAVE_QPDF
#include <memory>

class QPDF;

class QpdfContentProvider : public ContentProvider
{
public:
    QpdfContentProvider(const QString &filePath,
                        std::function<QSizeF(int)> pageSizePts);
    ~QpdfContentProvider() override;

protected:
    QList<ContentItem> buildPage(int page) override;

private:
    QString m_path;
    std::function<QSizeF(int)> m_pageSize;
    // Parsed file kept across buildPage calls — parsing the whole PDF once
    // instead of once per page. Recreated per file by DocumentView.
    std::unique_ptr<QPDF> m_qpdf;
};
#endif

// ── Poppler backend ───────────────────────────────────────────────────────────
#ifdef HAVE_POPPLER
namespace Poppler { class Document; }

class PopplerContentProvider : public ContentProvider
{
public:
    // doc is not owned; must outlive the provider.
    explicit PopplerContentProvider(Poppler::Document *doc);

protected:
    QList<ContentItem> buildPage(int page) override;

private:
    QList<ContentItem> buildPageUnguarded(int page);

    Poppler::Document *m_doc;
};
#endif
