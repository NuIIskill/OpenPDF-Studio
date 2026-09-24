#pragma once

#include "engine/historymanager/DocumentHistory.hpp"

#include <QString>

#include <functional>
#include <memory>

class DocumentSource;
class EditSession;

QT_BEGIN_NAMESPACE
class QUndoStack;
QT_END_NAMESPACE

/// The single way into the change log of the open document: what happened, what is unsaved, and how to go back.
class DocumentJournal
{
public:
    using StateSource = std::function<DocumentHistory::DocumentState()>;

    struct RestorePlan {
        bool    ok { false };
        int     index { -1 };
        QString reopenFile;
        QString target;
        int     undoIndex { 0 };
        DocumentHistory::DocumentState state;
        DocumentHistory::DocumentState before;
    };

    class RestoreGuard
    {
    public:
        explicit RestoreGuard(DocumentJournal &journal) : m_journal(journal)
        { m_journal.m_restoring = true; }
        ~RestoreGuard() { m_journal.m_restoring = false; }
        RestoreGuard(const RestoreGuard &) = delete;
        RestoreGuard &operator=(const RestoreGuard &) = delete;

    private:
        DocumentJournal &m_journal;
    };

    DocumentJournal();
    ~DocumentJournal();

    void attach(DocumentSource *src, QUndoStack *undo, StateSource state);
    void setSession(EditSession *session) { m_session = session; }

    const DocumentHistory *history() const { return m_history.get(); }

    QString currentFile() const;
    QString suggestedPath() const { return m_suggested; }
    QString displayName() const;
    bool    isRestoring() const { return m_restoring; }

    void opened(const QString &suggestedPath);
    void noteDocumentOpened(const DocumentHistory::Change &change);
    void openedAsWorkingCopy(const QString &targetPath);
    void contentMoved();
    void closed();
    void editsDiscarded();

    void recordChange(const DocumentHistory::Change &c,
                      const QString &snapshotSource = QString());
    void recordSideChange(const DocumentHistory::Change &c);
    void undoStackChanged();
    void prepareAnchor(const std::function<bool(const QString &path)> &write);
    void dropPendingAnchor();
    void clearHistory();

    void prepareSave();
    void markSaved(const QString &path);
    void savedOverBase(const QString &path, bool reopened);
    bool hasUnsavedEdits() const;

    QString copyToWorkingFile(const QString &source, const QString &like) const;
    void    discardCopy(const QString &path) const;

    RestorePlan planRestore(int index);
    [[nodiscard]] RestoreGuard restoring() { return RestoreGuard(*this); }
    void finishRestore(const RestorePlan &plan, bool ok);

    bool writeArchive(const QString &path);
    bool adoptArchive(const QString &path);

private:
    void recordSaved(const QString &path, const QString &snapshotSource);

    std::unique_ptr<DocumentHistory> m_history;
    DocumentSource  *m_src     { nullptr };
    EditSession     *m_session { nullptr };
    QUndoStack      *m_undo    { nullptr };
    StateSource      m_state;

    QString m_pendingAnchor;
    DocumentHistory::DocumentState m_pendingAnchorState;

    QString m_content;
    QString m_target;
    QString m_suggested;
    bool    m_workingCopyDirty { false };
    bool    m_restoring { false };

    quint64 m_savedImageRevision { 0 };
    quint64 m_sideRevision { 0 };
    quint64 m_savedSideRevision { 0 };
};
