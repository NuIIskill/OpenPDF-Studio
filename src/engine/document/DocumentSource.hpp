#pragma once

#include <QString>

#include <functional>
#include <memory>
#include <optional>

#ifdef HAVE_PDF_RENDERING
#  include "engine/edit/ContentModel.hpp"
#  include "engine/render/PdfRenderer.hpp"
#  ifdef HAVE_QT_PDF
#    include "engine/edit/PdfTextExtractor.hpp"
#    include <QPdfDocument>
#  elif defined(HAVE_POPPLER)
#    include <poppler/qt6/poppler-qt6.h>
#  endif
#endif

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

/// The document that is currently open: which file it is on disk, the backend
/// object holding it, and everything that reads from it — renderer, text
/// extractor, content model.
///
/// These belong together because they die together. The content provider holds
/// a raw pointer into the backend document, and the renderer holds another, so
/// tearing them down in the wrong order reads freed memory. That order used to
/// be a hand-written sequence in ~DocumentView with a comment asking the next
/// reader not to break it; here it is simply what the destructor does.
///
/// Deliberately a holder, not a loader. Opening a file has to be able to ask
/// the user for a password, which is a dialog and therefore cannot live in
/// engine/ — so the load path stays in the view for now. Giving the state one
/// owner is the half that can be done without that question; the loading half
/// moves here later behind a password callback, without touching any caller
/// of the accessors below again.
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

    /// Asked while an encrypted document refuses to open. `retry` is true after
    /// a rejected attempt, so the caller can say so. Returning nullopt means
    /// the user gave up and the load is abandoned.
    ///
    /// Only the asking is a callback — a password already stored for the file
    /// is tried first, and one that works is remembered, both without involving
    /// the caller. That part needs no dialog, so it does not need to leave here.
    using PasswordAsker =
        std::function<std::optional<QString>(const QString &file, bool retry)>;

    /// The PDF on disk being read from — a session working copy while changes
    /// are uncommitted, so this is not necessarily the file the user opened.
    QString contentPath() const { return m_contentPath; }
    void    setContentPath(const QString &path) { m_contentPath = path; }

    int  pageCount() const { return m_pageCount; }
    void setPageCount(int count) { m_pageCount = count; }

#ifdef HAVE_PDF_RENDERING
    PdfRenderer *renderer() const { return m_renderer; }

    /// Per-page region model (text, paragraphs, table cells, form fields,
    /// images). Null until it has been built for the open document.
    ContentProvider *contentProvider() const { return m_contentProvider.get(); }
    void setContentProvider(std::unique_ptr<ContentProvider> provider)
    { m_contentProvider = std::move(provider); }
    void clearContentProvider() { m_contentProvider.reset(); }

#  ifdef HAVE_QT_PDF
    QPdfDocument     *document()  const { return m_document; }
    PdfTextExtractor *extractor() const { return m_extractor; }

    /// Loads `path` into the document, asking through `ask` for as long as it
    /// is encrypted. Sets contentPath() and pageCount() when it worked.
    ///
    /// On failure the previously open file is loaded back, so a view showing
    /// this source keeps showing what it had. Without that, a rejected open
    /// left the document closed while the page widgets still described the old
    /// file — the view went blank with no error.
    bool load(const QString &path, const PasswordAsker &ask);
#  elif defined(HAVE_POPPLER)
    Poppler::Document *popplerDoc() const { return m_popplerDoc.get(); }

    /// Opens `path` as a NEW document without installing it. Asks through
    /// `ask` for as long as it stays locked; a null `ask` just tries the
    /// stored password and gives up. Returns null when it could not be opened.
    ///
    /// Deliberately hands the document back instead of installing it: whoever
    /// holds the current renderer has to let go before the old document dies,
    /// and only that holder knows when it has. Install with setPopplerDoc().
    std::unique_ptr<Poppler::Document> open(const QString &path,
                                            const PasswordAsker &ask) const;

    /// Exchanges the open document, renderer and content model in one step.
    /// Passing nullptr closes the document and releases the file. Callers that
    /// hand out the renderer must re-read it afterwards — the old one is gone.
    void setPopplerDoc(std::unique_ptr<Poppler::Document> doc);
#  endif
#endif

private:
    QString m_contentPath;
    int     m_pageCount { 0 };

#ifdef HAVE_PDF_RENDERING
    // Declared after the document on purpose, but the destructor does not rely
    // on member order — see the comment there.
    std::unique_ptr<ContentProvider> m_contentProvider;
    PdfRenderer *m_renderer { nullptr };
#  ifdef HAVE_QT_PDF
    QPdfDocument     *m_document  { nullptr };
    PdfTextExtractor *m_extractor { nullptr };
#  elif defined(HAVE_POPPLER)
    std::unique_ptr<Poppler::Document> m_popplerDoc;
#  endif
#endif
};
