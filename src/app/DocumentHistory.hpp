#pragma once

#include <QDateTime>
#include <QImage>
#include <QList>
#include <QObject>
#include <QRectF>
#include <QString>

// The change log of one open document — what the History panel shows and what
// "go back to this state" walks along.
//
// An entry is not a description of a change, it is a description of a STATE:
// the document as it looked once that change had been made. Restoring one is
// therefore always possible in principle; how it is done depends on where that
// state lives:
//
//   • Text edits are session overlays, so the undo stack already holds them.
//     The entry only remembers the stack index the state sits at (undoIndex).
//   • Images are session overlays too, but they carry no undo command, so the
//     entry keeps the overlay list itself. QImage is implicitly shared — the
//     copies cost a pointer each, not a bitmap.
//   • Page operations (rotate, delete, add, reorder) only exist as a written
//     PDF. Those entries own a snapshot file holding exactly that document.
//
// Every entry inherits the snapshot of the last entry that has one, so the
// snapshot marks where one document file ends and the next begins.
class DocumentHistory : public QObject
{
    Q_OBJECT

public:
    enum class Kind {
        Opened,          ///< document loaded (text = file name)
        TextEdited,      ///< text replaced on `page`
        TextRemoved,     ///< text erased on `page`
        ImageInserted,
        ImageRemoved,
        PageRotated,     ///< `value` = degrees, positive = clockwise
        PageDeleted,
        PageAdded,
        PagesReordered,
        PagesOrganized,  ///< several kinds of page change in one organizer run
        Saved,           ///< written to disk (text = file name)
        Reverted,        ///< restored an earlier state (value = its entry number)
    };

    /// Session image overlays as of one entry. Mirrors
    /// ImageAnnotationLayer::Placed, which lives in the UI layer.
    struct ImageState {
        int    page { -1 };
        QRectF pdfBounds;
        QImage image;
    };

    /// What a caller reports; the history adds time, state and ordering.
    struct Change {
        Kind    kind  { Kind::Opened };
        int     page  { -1 };   ///< 0-based, -1 = not about one page
        int     count { 1 };    ///< pages affected
        int     value { 0 };    ///< kind-specific (rotation angle, entry number)
        QString text;           ///< kind-specific (file name)
    };

    struct Entry {
        QDateTime        time;
        Kind             kind  { Kind::Opened };
        int              page  { -1 };
        int              count { 1 };
        int              value { 0 };
        QString          text;
        int              undoIndex { 0 };
        QList<ImageState> images;
        /// Document file this state lives in. Empty → the same file as the
        /// entry before it (see baseFileFor).
        QString          snapshot;
        /// Which document file the entry belongs to. Undo indices are only
        /// comparable within one of these: opening a file empties the undo
        /// stack, so index 2 before and index 2 after mean different states.
        int              base { 0 };
    };

    explicit DocumentHistory(QObject *parent = nullptr);
    ~DocumentHistory() override;

    const QList<Entry> &entries() const { return m_entries; }
    bool  isEmpty()      const { return m_entries.isEmpty(); }
    int   count()        const { return static_cast<int>(m_entries.size()); }
    /// Entry describing the state currently on screen (-1 while empty).
    int   currentIndex() const { return m_current; }

    enum class Snapshot {
        Copy,     ///< copy the file now — it is about to be replaced or dropped
        Deferred, ///< copy it only once something is about to change it
    };

    /// Appends a state. `snapshotSource` is the PDF file holding it — pass it
    /// for changes that only exist as a written document; an empty string
    /// leaves the entry on the file of the entry before it.
    ///
    /// Deferred is for a document that was merely opened: the file is sitting
    /// on disk unchanged, and copying it for a reader who never edits anything
    /// would be a copy of every PDF ever opened. materializeSnapshot() takes
    /// it the moment that file stops being a faithful copy of this state.
    void record(const Change &c, int undoIndex,
                const QList<ImageState> &images = {},
                const QString &snapshotSource = QString(),
                Snapshot mode = Snapshot::Copy);

    /// Takes the deferred snapshot, if one is outstanding. Called before
    /// anything writes to the file it is waiting on.
    void materializeSnapshot();

    /// Follows the state on screen without adding an entry — used when undo or
    /// redo moved the document to a state the history already knows.
    void setCurrentIndex(int index);
    /// Last entry that is at or below `undoIndex`, i.e. the state the undo
    /// stack is showing. -1 when the history is empty.
    int  indexForUndoIndex(int undoIndex) const;

    /// Document file the state of `index` lives in ("" if unknown).
    QString baseFileFor(int index) const;
    /// True when going back to `index` means reopening a different file than
    /// the one on screen — every session edit made since is lost then, because
    /// those edits were never part of any file.
    bool restoringDropsEdits(int index) const;
    /// False for states whose document file could not be kept (no disk space,
    /// no session directory): the entry still records what happened, it just
    /// cannot be gone back to.
    bool canRestore(int index) const;

    /// Forgets everything but the state on screen, which stays as entry 0.
    void clear();
    /// Forgets everything, including the state on screen (document closed).
    void reset();

Q_SIGNALS:
    /// Entries were added, removed, or the current one moved.
    void changed();

private:
    /// Undo index the document file of `index` was written at — the state that
    /// file alone can be put back into.
    int anchorUndoIndex(int index) const;

    /// Copies `sourcePath` into a snapshot this history owns. Empty on failure —
    /// the entry then simply has no file of its own, which costs restorability
    /// but never correctness.
    QString takeSnapshot(const QString &sourcePath);
    void    removeSnapshots();

    QList<Entry> m_entries;
    int          m_current { -1 };
    int          m_baseCounter { 0 };
    // Outstanding deferred snapshot: the entry waiting for it and the file it
    // is waiting on. -1 = none.
    int          m_pendingEntry { -1 };
    QString      m_pendingSource;
};
