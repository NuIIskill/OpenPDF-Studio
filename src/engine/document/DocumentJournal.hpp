#pragma once

#include "app/DocumentHistory.hpp"

#include <QString>

#include <functional>
#include <memory>

class DocumentSource;
class EditSession;

QT_BEGIN_NAMESPACE
class QUndoStack;
QT_END_NAMESPACE

/// What has happened to the open document, and what saving it would mean.
///
/// Six fields that answer one question between them — is this document saved,
/// where would it save to, and what changed since. They are inseparable in
/// practice: a save is also a history entry, restoring a history state
/// reopens a file, and "are there unsaved edits" is answered by comparing the
/// session against what the last save recorded.
///
/// The save target is kept apart from the file being read because they are not
/// the same thing. Page reordering, rotation and insertion produce a working
/// copy in the session directory; the view renders that copy while the user's
/// own PDF stays untouched, and only a save collapses the two.
class DocumentJournal
{
public:
    DocumentJournal() : m_history(std::make_unique<DocumentHistory>()) {}

    /// Where the image overlays currently sit. A history entry stores them
    /// next to the undo index, so recording a change has to ask for them —
    /// and they live in a widget layer this class cannot see.
    using ImageStateSource = std::function<QList<DocumentHistory::ImageState>()>;

    void attach(DocumentSource *src, QUndoStack *undo, ImageStateSource images)
    { m_src = src; m_undo = undo; m_images = std::move(images); }
    void setSession(EditSession *session) { m_session = session; }

    /// Change log of the open document — what the history panel shows.
    DocumentHistory *history() const { return m_history.get(); }

    /// Files a change under the open document. Does nothing while restoring:
    /// that walks the document through changes it has already recorded, and
    /// writing them down again would append the past to the log.
    void recordChange(const DocumentHistory::Change &c,
                      const QString &snapshotSource = QString());

    /// Called once a document is on screen. Either starts a fresh history, or
    /// — when a change produced this file, as page operations do — keeps the
    /// history running and files the file itself as the state to come back to.
    void noteDocumentOpened(const QString &displayName);

    /// Records that the session as it stands was written to `path`, without
    /// ending it. "Saved" is a mark on the session rather than the absence of
    /// edits, because the session lives on.
    void markSaved(const QString &path);

    /// Records a save that had to overwrite the document it was reading from,
    /// so the written file becomes the state the history points at.
    void recordSavedOverBase(const QString &path);

    /// Whether anything would be lost by closing now.
    bool hasUnsavedEdits() const;

    /// Save target while contentPath() holds a working copy. Empty means the
    /// file being read is also the file being written.
    QString targetPath;

    /// Changes that live only in the working copy, not in the edit session —
    /// they make the document dirty even though the session holds no edits.
    bool workingCopyDirty { false };

    /// What produced the working copy currently open, so a later save can file
    /// it under that change instead of starting the history over.
    DocumentHistory::Change openChange;

    /// Restoring a state reopens files and moves the undo stack — none of
    /// which is a change to record. Set while that is going on.
    bool restoring { false };

    /// Session image state as of the last successful save, so a document that
    /// has been saved does not keep claiming it has unsaved changes. Text
    /// edits are measured by the undo stack's clean marker instead, which
    quint64 savedImageRevision { 0 };

private:
    std::unique_ptr<DocumentHistory> m_history;
    DocumentSource  *m_src     { nullptr };
    EditSession     *m_session { nullptr };
    QUndoStack      *m_undo    { nullptr };
    ImageStateSource m_images;
};
