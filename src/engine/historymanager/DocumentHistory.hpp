#pragma once

#include "engine/document/PdfBookmark.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QRectF>
#include <QString>

#include <limits>

/// The change log of one open document: what the History panel shows and what "go back to this state" walks along.
class DocumentHistory : public QObject
{
    Q_OBJECT

public:
    enum class Kind {
        Opened,
        TextEdited,
        TextRemoved,
        ImageInserted,
        ImageRemoved,
        LinkAdded,
        LinkEdited,
        LinkRemoved,
        NoteAdded,
        NoteEdited,
        NoteRemoved,
        DrawingAdded,
        DrawingRemoved,
        BookmarksChanged,
        OverlayChanged,
        PageRotated,
        PageDeleted,
        PageAdded,
        PagesReordered,
        PagesOrganized,
        Saved,
        Reverted,
    };

    /// Session image overlays as of one entry.
    struct ImageState {
        int    page { -1 };
        QRectF pdfBounds;
        QImage image;
    };

    /// Everything outside the undo stack that an entry brings back.
    struct DocumentState {
        QList<ImageState>          images;
        QList<PdfBookmark>         bookmarks;
        QHash<QString, QByteArray> overlays;
    };

    /// What a caller reports; the history adds time, state and ordering.
    struct Change {
        Kind    kind  { Kind::Opened };
        int     page  { -1 };
        int     count { 1 };
        int     value { 0 };
        QString text;
    };

    struct Entry {
        QDateTime        time;
        Kind             kind  { Kind::Opened };
        int              page  { -1 };
        int              count { 1 };
        int              value { 0 };
        QString          text;
        int              undoIndex { 0 };
        DocumentState    state;

        QString          snapshot;

        int              base { 0 };
    };

    explicit DocumentHistory(QObject *parent = nullptr);
    ~DocumentHistory() override;

    const QList<Entry> &entries() const { return m_entries; }
    bool  isEmpty()      const { return m_entries.isEmpty(); }
    int   count()        const { return static_cast<int>(m_entries.size()); }

    int   currentIndex() const { return m_current; }

    enum class Snapshot {
        Copy,
        Deferred,
    };

    void record(const Change &c, int undoIndex,
                const DocumentState &state = {},
                const QString &snapshotSource = QString(),
                Snapshot mode = Snapshot::Copy);

    void materializeSnapshot();

    void setCurrentIndex(int index);

    void setUndoDepth(int depth);

    bool currentIsAnchored() const;
    void anchorCurrent(const QString &snapshot, const DocumentState &state);

    QString baseFileFor(int index) const;

    bool restoringDropsEdits(int index) const;

    bool canRestore(int index) const;

    void clear();

    void reset();

    void adopt(QList<Entry> entries, int current, const QString &currentSource,
               const DocumentState &currentState, int undoIndex);

Q_SIGNALS:

    void changed();

private:

    int anchorUndoIndex(int index) const;

    QString takeSnapshot(const QString &sourcePath);
    void    removeSnapshots();

    QList<Entry> m_entries;
    int          m_current { -1 };
    int          m_baseCounter { 0 };
    int          m_undoDepth { std::numeric_limits<int>::max() };

    int          m_pendingEntry { -1 };
    QString      m_pendingSource;
};
