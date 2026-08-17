#include "engine/document/DocumentJournal.hpp"

#include "engine/document/DocumentSource.hpp"

#include <QFileInfo>
#include <QUndoStack>

#ifdef HAVE_PDF_RENDERING
#  include "engine/edit/EditSession.hpp"
#endif

void DocumentJournal::recordSavedOverBase(const QString &path)
{
    // This save could not keep the document it was layered on: the edits are
    // in the file now and the session started over. The written file is
    // therefore the state to come back to — the undo indices recorded before
    // it belong to a stack that no longer exists.
    DocumentHistory::Change saved;
    saved.kind  = DocumentHistory::Kind::Saved;
    saved.count = m_src->pageCount();
    saved.text  = QFileInfo(path).fileName();
    recordChange(saved, m_src->contentPath());
}

void DocumentJournal::recordChange(const DocumentHistory::Change &c,
                                const QString &snapshotSource)
{
    // Restoring a state walks the document through changes it has already
    // recorded — writing them down again would append the past to the log.
    if (restoring || m_src->contentPath().isEmpty()) return;
    m_history->record(c, m_undo->index(), m_images(), snapshotSource);
}

void DocumentJournal::noteDocumentOpened(const QString &displayName)
{
    if (restoring) return;

    if (openChange.kind != DocumentHistory::Kind::Opened) {
        // A change produced this file (page operations). The history keeps
        // running; the file itself becomes the state to come back to.
        m_history->record(openChange, m_undo->index(), m_images(),
                          m_src->contentPath());
        return;
    }

    // A document the user opened: everything recorded so far belongs to the
    // one before it.
    m_history->reset();
    DocumentHistory::Change opened;
    opened.kind  = DocumentHistory::Kind::Opened;
    opened.count = m_src->pageCount();
    opened.text  = displayName;
    // The file is on disk and nothing has touched it — no reason to copy it
    // until something is about to.
    m_history->record(opened, m_undo->index(), m_images(), m_src->contentPath(),
                      DocumentHistory::Snapshot::Deferred);
}

#ifdef HAVE_PDF_RENDERING
void DocumentJournal::markSaved(const QString &path)
{
    // The session lives on, so "saved" is a mark on it rather than the absence
    // of edits: the undo stack's clean index follows undo AND redo, so stepping
    // back past the save reports unsaved changes again and stepping forward to
    // it reports none.
    m_undo->setClean();
    savedImageRevision = m_session->imageRevision();
    workingCopyDirty   = false;
    // m_src->contentPath() is the session's own base now; `path` is the document the user
    // thinks of as open, which is what the title bar has to show.
    targetPath = (QFileInfo(path).absoluteFilePath()
                        == QFileInfo(m_src->contentPath()).absoluteFilePath())
                       ? QString() : path;

    DocumentHistory::Change saved;
    saved.kind  = DocumentHistory::Kind::Saved;
    saved.count = m_src->pageCount();
    saved.text  = QFileInfo(path).fileName();
    recordChange(saved);

}
#endif

// ── Change history ────────────────────────────────────────────────────────────

bool DocumentJournal::hasUnsavedEdits() const
{
    // Page changes live in the working copy, not in the session — without this
    // the close prompt would let a reorganized document go unsaved silently.
    if (workingCopyDirty) return true;
#ifdef HAVE_PDF_RENDERING
    if (!m_session) return false;
    // Text edits: the undo stack knows whether the document is back at the
    // state that was written. Images carry no undo command, so they are
    // compared against the revision the save recorded.
    return !m_undo->isClean()
        || m_session->imageRevision() != savedImageRevision;
#else
    return false;
#endif
}
