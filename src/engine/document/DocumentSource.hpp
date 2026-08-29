#pragma once

#include <QString>

#include <memory>

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/PdfBackend.hpp"
#  include "engine/edit/ContentModel.hpp"
#  include "engine/render/PdfRenderer.hpp"
#endif

/// Owns the backend and derived services for the open document.
class DocumentSource
{
public:
    DocumentSource();
    ~DocumentSource();

    DocumentSource(const DocumentSource &)            = delete;
    DocumentSource &operator=(const DocumentSource &) = delete;

    static constexpr bool renderingAvailable()
    {
#ifdef HAVE_PDF_RENDERING
        return true;
#else
        return false;
#endif
    }

#ifdef HAVE_PDF_RENDERING
    using PasswordAsker = PdfBackend::PasswordAsker;
#else
    using PasswordAsker =
        std::function<std::optional<QString>(const QString &file, bool retry)>;
#endif

    QString contentPath() const { return m_contentPath; }
    void    setContentPath(const QString &path) { m_contentPath = path; }

    int  pageCount() const { return m_pageCount; }
    void setPageCount(int count) { m_pageCount = count; }

#ifdef HAVE_PDF_RENDERING

    bool open(const QString &path, const PasswordAsker &ask);

    void close();

    PdfBackend  *backend()  const { return m_backend.get(); }
    PdfRenderer *renderer() const { return m_renderer; }

    ContentProvider *contentProvider() const { return m_contentProvider.get(); }
    void setContentProvider(std::unique_ptr<ContentProvider> provider)
    { m_contentProvider = std::move(provider); }
    void clearContentProvider() { m_contentProvider.reset(); }

    void resetContentProvider();

#endif

private:
    QString m_contentPath;
    int     m_pageCount { 0 };

#ifdef HAVE_PDF_RENDERING

    std::unique_ptr<ContentProvider> m_contentProvider;
    PdfRenderer                     *m_renderer { nullptr };
    std::unique_ptr<PdfBackend>      m_backend;
#endif
};
