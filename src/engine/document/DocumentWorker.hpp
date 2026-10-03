#pragma once

#ifdef HAVE_PDF_RENDERING

#include "engine/document/PdfBackend.hpp"

#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QRect>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>

class EditSession;
class QThread;
class RenderPool;

/// Renders pages, reads annotations and writes copies of one document on a background thread.
class DocumentWorker : public QObject
{
    Q_OBJECT

public:
    struct Render {
        int   page    { -1 };
        qreal scale   { 1.0 };
        int   zoom    { 0 };
        int   version { 0 };
        QRect area;
        std::shared_ptr<const EditSession> session;
        bool  finish  { false };
    };

    explicit DocumentWorker(PdfBackend *backend);
    ~DocumentWorker() override;

    void setRenders(const QList<Render> &renders);
    void readAnnotations(const QList<int> &pages);
    void setFocusPage(int page) { m_focusPage = page; }
    void writeCopy(const QString &output, std::shared_ptr<const EditSession> session);
    void stop();
    void setDocument(const QString &path);
    void findText(int search, const QString &needle);

    std::optional<Render> takeForHelper(int helper);
    void helperDone(int helper, const QImage &image, int milliseconds);
    void helpersChanged();

Q_SIGNALS:
    void pageRendered(int page, int zoom, int version, const QRect &area, const QImage &image,
                      int milliseconds);
    void annotationsRead(int page, const QList<PdfBackend::Link> &links,
                         const QList<PdfBackend::Note> &notes);
    void copyWritten(const QString &output, bool ok);
    void textFound(int search, int page, const QList<PdfBackend::TextMatch> &matches);
    void searchDone(int search);
    void stopped();

private:
    struct Copy {
        QString output;
        std::shared_ptr<const EditSession> session;
    };

    void run();
    void runRender(const Render &render, quint64 generation);
    void runCopy(const Copy &copy, quint64 generation);
    void runAnnotations(int page, quint64 generation);
    void runSearch(int search, const QString &needle, int page, bool last, quint64 generation);
    void post(quint64 generation, std::function<void()> emitter);
    void postCopyFailed(const QString &output);
    bool forHelpers(const Render &render) const;
    void useHelpers();
    bool hasOwnWork() const;

    PdfBackend *m_backend { nullptr };
    QThread    *m_thread  { nullptr };

    std::mutex              m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_idle;
    QList<Render>           m_renders;
    QList<int>              m_annotationPages;
    QList<Copy>             m_copies;
    struct Search { int id = 0; QString needle; int next = 0; };
    Search                  m_search;
    bool                    m_searchTurn { false };
    std::optional<Render>   m_runningRender;

    /// A render a helper process works on, and whether it is still wanted.
    struct HelperJob { Render render; quint64 generation = 0; bool cancelled = false; };
    QHash<int, HelperJob>   m_helperJobs;
    QHash<int, QList<int>>  m_helperPages;
    RenderPool             *m_pool { nullptr };
    QString                 m_documentPath;
    bool                    m_helpersOpen { false };
    std::atomic<int>        m_slowRenders { 0 };
    QString                 m_runningCopy;
    bool                    m_busy { false };
    bool                    m_quit { false };

    std::atomic<int>     m_focusPage    { 0 };
    std::atomic<bool>    m_cancelRender { false };
    std::atomic<bool>    m_cancelCopy   { false };
    std::atomic<quint64> m_generation   { 0 };
};

#endif
