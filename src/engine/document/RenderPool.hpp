#pragma once

#ifdef HAVE_PDF_RENDERING

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include <atomic>

class DocumentWorker;
class QLocalServer;
class QLocalSocket;
class QProcess;
class QThread;

/// Runs helper processes that render a worker's unedited pages in parallel, each with its own PDFium.
class RenderPool : public QObject
{
    Q_OBJECT

public:
    explicit RenderPool(DocumentWorker *worker);
    ~RenderPool() override;

    static void setHelper(const QString &program);
    static bool available();

    void requestDispatch();
    void requestCancel(int helper);
    void requestOpen(const QString &path, const QString &password);
    void closeDocument();

    int liveHelpers() const { return m_live.load(); }

private:
    struct Helper {
        QLocalSocket *socket  { nullptr };
        QByteArray    buffer;
        int           pending { 0 };
        bool          open    { false };
        bool          busy    { false };
        quint64       job     { 0 };
    };

    void start();
    void shutdown();
    void dispatch();
    void cancel(int helper);
    void open(const QString &path, const QString &password);
    void close();
    void connectHelper();
    void readFrom(int helper);
    void handle(int helper, const QByteArray &frame);
    void lose(int helper);
    void countLive();

    DocumentWorker   *m_worker { nullptr };
    QThread          *m_thread { nullptr };
    QLocalServer     *m_server { nullptr };
    QList<QProcess *> m_processes;
    QList<Helper>     m_helpers;
    QString           m_path;
    QString           m_password;
    quint64           m_nextJob { 1 };
    std::atomic<int>  m_live { 0 };
};

#endif
