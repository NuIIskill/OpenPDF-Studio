#pragma once

#include <QDateTime>
#include <QImage>
#include <QList>
#include <QObject>
#include <QRectF>
#include <QString>

/// The change log of one open document — what the History panel shows and what "go back to this state" walks along.
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
        QList<ImageState> images;

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
                const QList<ImageState> &images = {},
                const QString &snapshotSource = QString(),
                Snapshot mode = Snapshot::Copy);

    void materializeSnapshot();

    void setCurrentIndex(int index);

    int  indexForUndoIndex(int undoIndex) const;

    QString baseFileFor(int index) const;

    bool restoringDropsEdits(int index) const;

    bool canRestore(int index) const;

    void clear();

    void reset();

Q_SIGNALS:

    void changed();

private:

    int anchorUndoIndex(int index) const;

    QString takeSnapshot(const QString &sourcePath);
    void    removeSnapshots();

    QList<Entry> m_entries;
    int          m_current { -1 };
    int          m_baseCounter { 0 };

    int          m_pendingEntry { -1 };
    QString      m_pendingSource;
};
