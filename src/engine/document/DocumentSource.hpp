#pragma once

#include <QString>

#include <memory>

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/PdfBackend.hpp"
#  include "engine/edit/ContentModel.hpp"
#  include "engine/render/PdfRenderer.hpp"
#endif

/// The document that is currently open: which file it is on disk, the backend
/// holding it, and everything that reads from it — renderer, content model.
///
/// These belong together because they die together: the content provider holds
/// a pointer into the backend's document, so tearing them down in the wrong
/// order reads freed memory. That order used to be a hand-written sequence in
/// ~DocumentView with a comment asking the next reader not to break it; here it
/// is simply what the destructor does.
///
/// Since the backend owns both the document and the rendering, opening a file
/// is one call — no more per-file renderer, no more handing fresh pointers to
/// four collaborators in the right order.
class DocumentSource
{
public:
    DocumentSource();
    ~DocumentSource();

    DocumentSource(const DocumentSource &)            = delete;
    DocumentSource &operator=(const DocumentSource &) = delete;

    /// Whether this build can render PDFs at all.
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

    /// The PDF on disk being read from — a session working copy while changes
    /// are uncommitted, so this is not necessarily the file the user opened.
    QString contentPath() const { return m_contentPath; }
    void    setContentPath(const QString &path) { m_contentPath = path; }

    int  pageCount() const { return m_pageCount; }
    void setPageCount(int count) { m_pageCount = count; }

#ifdef HAVE_PDF_RENDERING
    /// Opens `path`, asking through `ask` while it stays encrypted. On success
    /// contentPath() and pageCount() describe the new file; on failure the
    /// previous document keeps working and nothing here changes.
    bool open(const QString &path, const PasswordAsker &ask);

    /// Closes the document and releases the file — required before overwriting
    /// the file that is open, which Windows otherwise refuses.
    void close();

    PdfBackend  *backend()  const { return m_backend.get(); }
    PdfRenderer *renderer() const { return m_renderer; }

    /// Per-page region model (text, paragraphs, table cells, form fields,
    /// images). Null until it has been built for the open document.
    ContentProvider *contentProvider() const { return m_contentProvider.get(); }
    void setContentProvider(std::unique_ptr<ContentProvider> provider)
    { m_contentProvider = std::move(provider); }
    void clearContentProvider() { m_contentProvider.reset(); }

    /// Rebuilds the content model for the open document.
    void resetContentProvider();

#endif

private:
    QString m_contentPath;
    int     m_pageCount { 0 };

#ifdef HAVE_PDF_RENDERING
    // Destroyed in this order by the destructor, not by declaration order:
    // provider first (it points into the document), then renderer, then backend.
    std::unique_ptr<ContentProvider> m_contentProvider;
    PdfRenderer                     *m_renderer { nullptr };
    std::unique_ptr<PdfBackend>      m_backend;
#endif
};
