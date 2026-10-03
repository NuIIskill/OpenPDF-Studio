#pragma once

#include <QBitArray>
#include <QList>
#include <QObject>

class DocumentWorker;
class LinkAnnotationLayer;
class NoteLayer;
class PdfBackend;

/// Fills the link and note layers page by page from the background worker.
class AnnotationLoader : public QObject
{
    Q_OBJECT

public:
    AnnotationLoader(LinkAnnotationLayer *links, NoteLayer *notes, QObject *parent = nullptr);

    void setSource(PdfBackend *backend, DocumentWorker *worker);

    void start(int pageCount);
    void finish();

private:
    QList<int> remainingPages() const;
    void readHere(int budgetMs);
    void resume();

    LinkAnnotationLayer *m_links   { nullptr };
    NoteLayer           *m_notes   { nullptr };
    PdfBackend          *m_backend { nullptr };
    DocumentWorker      *m_worker  { nullptr };
    QBitArray            m_loaded;
    int                  m_remaining { 0 };
};
