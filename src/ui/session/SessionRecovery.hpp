#pragma once

#include "app/SessionStore.hpp"

#include <QHash>
#include <QList>
#include <QObject>

class DocumentView;

QT_BEGIN_NAMESPACE
class QTimer;
class QWidget;
QT_END_NAMESPACE

/// Keeps every open document restorable and brings the session back after a crash.
class SessionRecovery : public QObject
{
    Q_OBJECT

public:
    explicit SessionRecovery(QWidget *parent);
    ~SessionRecovery() override;

    QList<SessionStore::OpenDocument> offerAbandonedDocuments();

    static void discard(const SessionStore::OpenDocument &doc);

    void begin();
    void watch(DocumentView *view);
    void forget(DocumentView *view);
    void finish();

private:
    struct Copy {
        QString path;
        QString archive;
        bool    stale     { false };
        qint64  changedAt { 0 };
        qint64  writtenAt { 0 };
    };

    void tick();
    void noteChange(DocumentView *view);
    void dropCopy(DocumentView *view);
    bool writeContent(DocumentView *view, Copy &copy);
    bool writeArchive(DocumentView *view, Copy &copy);
    void syncManifest();

    QWidget               *m_parent { nullptr };
    QTimer                *m_timer  { nullptr };
    QList<DocumentView *>  m_views;
    QHash<DocumentView *, Copy> m_copies;
    bool                   m_active { false };
};
