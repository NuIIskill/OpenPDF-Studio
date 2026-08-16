#include "engine/document/DocumentSource.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "app/PdfPwStore.hpp"
#  include <QDebug>
#endif

DocumentSource::DocumentSource()
{
#if defined(HAVE_PDF_RENDERING) && defined(HAVE_QT_PDF)
    // The Qt PDF backend keeps one document object for the lifetime of the
    // view and loads into it; only the Poppler path swaps the object per file.
    m_document  = new QPdfDocument;
    m_renderer  = new PdfRenderer(m_document);
    m_extractor = new PdfTextExtractor(m_document);
#endif
}

DocumentSource::~DocumentSource()
{
#ifdef HAVE_PDF_RENDERING
    // The provider references the backend document (raw pointer on Poppler),
    // so it has to go first — explicitly, not by member declaration order,
    // which is too easy to reorder by accident.
    m_contentProvider.reset();
    delete m_renderer;
#  ifdef HAVE_QT_PDF
    delete m_extractor;
    delete m_document;
#  elif defined(HAVE_POPPLER)
    // m_popplerDoc (unique_ptr) cleaned up automatically, after the renderer
    // that points into it.
#  endif
#endif
}

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_QT_PDF)
bool DocumentSource::load(const QString &path, const PasswordAsker &ask)
{
    // Load before dropping any state: a failed load used to leave the session
    // cleared and the document closed while the page widgets and page count
    // still described the previous file — the view went blank with no error.
    // QPdfDocument requires an explicit close before loading another file.
    m_document->close();
    // A password already known for this file (reopen, working copy) is tried
    // before the user is asked again.
    m_document->setPassword(PdfPwStore::get(path));
    auto err = m_document->load(path);

    // Encrypted documents used to fail here with nothing but a log line: the
    // view went blank and never said why. Ask, and keep asking until it opens
    // or the user gives up.
    for (int attempt = 0; ask && err == QPdfDocument::Error::IncorrectPassword;
         ++attempt) {
        const std::optional<QString> entered = ask(path, attempt > 0);
        if (!entered) break;                            // cancelled

        m_document->close();
        m_document->setPassword(*entered);
        err = m_document->load(path);
        if (err == QPdfDocument::Error::None) {
            // Every other reader of this file — content scanner, edit session,
            // exporter — picks the password up from here.
            PdfPwStore::set(path, *entered);
        }
    }

    if (err != QPdfDocument::Error::None) {
        qWarning() << "DocumentSource: could not open" << path << "-" << err;
        // Put the previous document back so the view keeps showing it.
        m_document->close();
        if (!m_contentPath.isEmpty() && m_contentPath != path) {
            m_document->setPassword(PdfPwStore::get(m_contentPath));
            m_document->load(m_contentPath);
        }
        return false;
    }

    m_contentPath = path;
    m_pageCount   = m_document->pageCount();
    return true;
}
#endif

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_POPPLER)
std::unique_ptr<Poppler::Document> DocumentSource::open(const QString &path,
                                                        const PasswordAsker &ask) const
{
    if (path.isEmpty()) return nullptr;
    auto doc = Poppler::Document::load(path);

    // The Poppler backend needs the same password handling as the Qt one — it
    // is what the Windows build ships. Without this an encrypted document just
    // returned false here and the window stayed empty with no explanation.
    if (doc && doc->isLocked()) {
        const QByteArray known = PdfPwStore::get(path).toUtf8();
        if (!known.isEmpty()) doc->unlock(known, known);
    }
    for (int attempt = 0; ask && doc && doc->isLocked(); ++attempt) {
        const std::optional<QString> entered = ask(path, attempt > 0);
        if (!entered) break;                            // cancelled
        const QByteArray pw = entered->toUtf8();
        doc->unlock(pw, pw);
        if (!doc->isLocked()) PdfPwStore::set(path, *entered);
    }

    if (!doc || doc->isLocked()) return nullptr;
    doc->setRenderHint(Poppler::Document::Antialiasing);
    doc->setRenderHint(Poppler::Document::TextAntialiasing);
    return doc;
}

void DocumentSource::setPopplerDoc(std::unique_ptr<Poppler::Document> doc)
{
    m_contentProvider.reset();   // references the old doc — drop it first
    delete m_renderer;
    m_renderer = nullptr;

    m_popplerDoc = std::move(doc);
    if (!m_popplerDoc) return;   // closed: the file is free to be replaced now

    m_renderer = new PdfRenderer(m_popplerDoc.get());
}
#endif
