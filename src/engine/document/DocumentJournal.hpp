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
class DocumentJournal
{
public:
    DocumentJournal() : m_history(std::make_unique<DocumentHistory>()) {}

    using ImageStateSource = std::function<QList<DocumentHistory::ImageState>()>;

    void attach(DocumentSource *src, QUndoStack *undo, ImageStateSource images)
    { m_src = src; m_undo = undo; m_images = std::move(images); }
    void setSession(EditSession *session) { m_session = session; }

    DocumentHistory *history() const { return m_history.get(); }

    void recordChange(const DocumentHistory::Change &c,
                      const QString &snapshotSource = QString());

    void noteDocumentOpened(const QString &displayName);

    void markSaved(const QString &path);

    void recordSavedOverBase(const QString &path);

    bool hasUnsavedEdits() const;

    QString targetPath;

    bool workingCopyDirty { false };

    DocumentHistory::Change openChange;

    bool restoring { false };

    quint64 savedImageRevision { 0 };

private:
    std::unique_ptr<DocumentHistory> m_history;
    DocumentSource  *m_src     { nullptr };
    EditSession     *m_session { nullptr };
    QUndoStack      *m_undo    { nullptr };
    ImageStateSource m_images;
};
