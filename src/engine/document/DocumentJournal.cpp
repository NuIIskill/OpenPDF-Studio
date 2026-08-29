#include "engine/document/DocumentJournal.hpp"

#include "engine/document/DocumentSource.hpp"

#include <QFileInfo>
#include <QUndoStack>

#ifdef HAVE_PDF_RENDERING
#  include "engine/edit/EditSession.hpp"
#endif

void DocumentJournal::recordSavedOverBase(const QString &path)
{

    DocumentHistory::Change saved;
    saved.kind  = DocumentHistory::Kind::Saved;
    saved.count = m_src->pageCount();
    saved.text  = QFileInfo(path).fileName();
    recordChange(saved, m_src->contentPath());
}

void DocumentJournal::recordChange(const DocumentHistory::Change &c,
                                const QString &snapshotSource)
{

    if (restoring || m_src->contentPath().isEmpty()) return;
    m_history->record(c, m_undo->index(), m_images(), snapshotSource);
}

void DocumentJournal::noteDocumentOpened(const QString &displayName)
{
    if (restoring) return;

    if (openChange.kind != DocumentHistory::Kind::Opened) {

        m_history->record(openChange, m_undo->index(), m_images(),
                          m_src->contentPath());
        return;
    }

    m_history->reset();
    DocumentHistory::Change opened;
    opened.kind  = DocumentHistory::Kind::Opened;
    opened.count = m_src->pageCount();
    opened.text  = displayName;

    m_history->record(opened, m_undo->index(), m_images(), m_src->contentPath(),
                      DocumentHistory::Snapshot::Deferred);
}

#ifdef HAVE_PDF_RENDERING
void DocumentJournal::markSaved(const QString &path)
{

    m_undo->setClean();
    savedImageRevision = m_session->imageRevision();
    workingCopyDirty   = false;

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

bool DocumentJournal::hasUnsavedEdits() const
{

    if (workingCopyDirty) return true;
#ifdef HAVE_PDF_RENDERING
    if (!m_session) return false;

    return !m_undo->isClean()
        || m_session->imageRevision() != savedImageRevision;
#else
    return false;
#endif
}
