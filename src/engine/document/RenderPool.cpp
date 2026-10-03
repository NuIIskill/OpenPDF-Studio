#include "engine/document/RenderPool.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/document/DocumentWorker.hpp"
#include "engine/document/RenderServer.hpp"
#include "app/SystemMemory.hpp"

#include <QCoreApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QImage>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QThread>

#include <atomic>

#if defined(Q_OS_UNIX)
#  include <unistd.h>
#elif defined(Q_OS_WIN)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif


using RenderServer::Message;

namespace {

QString &helperProgram()
{
    static QString program;
    return program;
}

QString program()
{
    // Tests run as their own executable and name the application to use.
    const QString fromEnv = qEnvironmentVariable("OPENPDF_RENDER_HELPER");
    return fromEnv.isEmpty() ? helperProgram() : fromEnv;
}

int helperCount()
{
    if (qEnvironmentVariableIsSet("OPENPDF_RENDER_PROCESSES"))
        return qMax(0, qEnvironmentVariableIntValue("OPENPDF_RENDER_PROCESSES"));
    // Half the cores, one kept for the window, and about 400 MB of free memory
    // each, which a helper decoding large images can take.
    int count = qBound(0, QThread::idealThreadCount() / 2 - 1, 6);
    const qint64 freeMb = SystemMemory::availableMb();
    if (freeMb >= 0) count = qMin(count, int(freeMb / 400));
    return count;
}

QByteArray message(Message type, const std::function<void(QDataStream &)> &fill = {})
{
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out << quint8(type);
    if (fill) fill(out);
    return payload;
}

}

void RenderPool::setHelper(const QString &program)
{
    helperProgram() = program;
}

bool RenderPool::available()
{
    return !program().isEmpty() && helperCount() > 0;
}

RenderPool::RenderPool(DocumentWorker *worker)
    : m_worker(worker)
{
    m_thread = new QThread;
    moveToThread(m_thread);
    m_thread->start();
    QMetaObject::invokeMethod(this, &RenderPool::start, Qt::QueuedConnection);
}

RenderPool::~RenderPool()
{
    QMetaObject::invokeMethod(this, &RenderPool::shutdown, Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
    delete m_thread;
}

void RenderPool::requestDispatch()
{
    QMetaObject::invokeMethod(this, &RenderPool::dispatch, Qt::QueuedConnection);
}

void RenderPool::requestCancel(int helper)
{
    QMetaObject::invokeMethod(this, [this, helper] { cancel(helper); }, Qt::QueuedConnection);
}

void RenderPool::requestOpen(const QString &path, const QString &password)
{
    QMetaObject::invokeMethod(this, [this, path, password] { open(path, password); },
                              Qt::QueuedConnection);
}

void RenderPool::closeDocument()
{
    QMetaObject::invokeMethod(this, &RenderPool::close, Qt::BlockingQueuedConnection);
}

void RenderPool::start()
{
    static std::atomic<int> counter { 0 };
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    const QString name = QStringLiteral("openpdf-render-%1-%2")
                             .arg(QCoreApplication::applicationPid()).arg(++counter);
    if (!m_server->listen(name)) return;
    connect(m_server, &QLocalServer::newConnection, this, &RenderPool::connectHelper);
    for (int i = 0; i < helperCount(); ++i) {
        auto *process = new QProcess(this);
        process->setProcessChannelMode(QProcess::ForwardedChannels);
        // Helpers yield to the window, which matters on machines with few cores.
#if defined(Q_OS_UNIX)
        process->setChildProcessModifier([] { (void)::nice(10); });
#elif defined(Q_OS_WIN)
        process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
            args->flags |= BELOW_NORMAL_PRIORITY_CLASS;
        });
#endif
        process->start(program(), { QStringLiteral("--render-server"), m_server->fullServerName() });
        m_processes.append(process);
    }
}

void RenderPool::shutdown()
{
    for (Helper &helper : m_helpers) {
        if (!helper.socket) continue;
        RenderServer::send(*helper.socket, message(Message::Quit));
        helper.socket->flush();
    }
    for (QProcess *process : std::as_const(m_processes)) {
        if (!process->waitForFinished(300)) process->kill();
        process->waitForFinished(300);
        delete process;
    }
    m_processes.clear();
    for (Helper &helper : m_helpers) delete helper.socket;
    m_helpers.clear();
    delete m_server;
    m_server = nullptr;
    m_live = 0;
}

void RenderPool::connectHelper()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        const int index = int(m_helpers.size());
        m_helpers.append(Helper { socket });
        connect(socket, &QLocalSocket::readyRead, this, [this, index] { readFrom(index); });
        connect(socket, &QLocalSocket::disconnected, this, [this, index] { lose(index); });
        if (!m_path.isEmpty()) {
            ++m_helpers[index].pending;
            RenderServer::send(*socket, message(Message::Open, [this](QDataStream &out) {
                out << m_path << m_password;
            }));
        }
    }
}

void RenderPool::open(const QString &path, const QString &password)
{
    m_path     = path;
    m_password = password;
    for (Helper &helper : m_helpers) {
        if (!helper.socket) continue;
        helper.open = false;
        ++helper.pending;
        RenderServer::send(*helper.socket, message(Message::Open, [this](QDataStream &out) {
            out << m_path << m_password;
        }));
    }
    countLive();
}

void RenderPool::close()
{
    // The helpers let go of the file before anything may replace it.
    m_path.clear();
    m_password.clear();
    for (Helper &helper : m_helpers) {
        if (!helper.socket) continue;
        helper.open = false;
        ++helper.pending;
        RenderServer::send(*helper.socket, message(Message::Close));
        helper.socket->flush();
    }
    countLive();
    QElapsedTimer clock;
    clock.start();
    for (int i = 0; i < m_helpers.size(); ++i) {
        while (m_helpers[i].socket && m_helpers[i].pending > 0) {
            const qint64 left = 2000 - clock.elapsed();
            if (left <= 0 || !m_helpers[i].socket->waitForReadyRead(int(left))) {
                lose(i);
                break;
            }
            readFrom(i);
        }
    }
}

void RenderPool::dispatch()
{
    for (int i = 0; i < m_helpers.size(); ++i) {
        Helper &helper = m_helpers[i];
        if (!helper.socket || !helper.open || helper.busy || helper.pending > 0) continue;
        const std::optional<DocumentWorker::Render> render = m_worker->takeForHelper(i);
        if (!render) return;
        helper.busy = true;
        helper.job  = m_nextJob++;

        RenderServer::send(*helper.socket, message(Message::Render, [&](QDataStream &out) {
            out << helper.job << qint32(render->page) << double(render->scale) << render->area;
        }));
    }
}

void RenderPool::cancel(int helper)
{
    if (helper < 0 || helper >= m_helpers.size()) return;
    Helper &h = m_helpers[helper];
    if (h.socket && h.busy) RenderServer::send(*h.socket, message(Message::Cancel));
}

void RenderPool::readFrom(int helper)
{
    Helper &h = m_helpers[helper];
    if (!h.socket) return;
    h.buffer += h.socket->readAll();
    QByteArray frame;
    while (m_helpers[helper].socket && RenderServer::takeFrame(m_helpers[helper].buffer, frame))
        handle(helper, frame);
}

void RenderPool::handle(int helper, const QByteArray &frame)
{
    Helper &h = m_helpers[helper];
    QDataStream in(frame);
    quint8 type = 0;
    in >> type;
    switch (Message(type)) {
    case Message::Opened: {
        bool ok = false;
        in >> ok;
        if (--h.pending == 0) h.open = ok && !m_path.isEmpty();
        countLive();
        dispatch();
        break;
    }
    case Message::Closed:
        --h.pending;
        break;
    case Message::Rendered: {
        quint64 job = 0;
        qint32 ms = 0, width = 0, height = 0, stride = 0, format = 0;
        in >> job >> ms >> width >> height >> stride >> format;
        const qsizetype offset = frame.size() - qsizetype(stride) * height;
        QImage image;
        if (job == h.job && offset >= 0 && width > 0 && height > 0) {
            image = QImage(width, height, QImage::Format(format));
            for (int y = 0; y < height; ++y)
                memcpy(image.scanLine(y), frame.constData() + offset + qsizetype(y) * stride,
                       size_t(qMin<qsizetype>(stride, image.bytesPerLine())));
        }
        h.busy = false;
        m_worker->helperDone(helper, image, ms);
        dispatch();
        break;
    }
    case Message::Failed:
        h.busy = false;
        m_worker->helperDone(helper, {}, 0);
        dispatch();
        break;
    default:
        break;
    }
}

void RenderPool::lose(int helper)
{
    // A helper that crashed or did not answer is left out from now on.
    Helper &h = m_helpers[helper];
    if (!h.socket) return;
    QLocalSocket *socket = h.socket;
    h.socket = nullptr;
    h.open   = false;
    socket->abort();
    socket->deleteLater();
    if (h.busy) {
        h.busy = false;
        m_worker->helperDone(helper, {}, 0);
    }
    countLive();
}

void RenderPool::countLive()
{
    int live = 0;
    for (const Helper &helper : std::as_const(m_helpers))
        if (helper.socket && helper.open) ++live;
    m_live = live;
    m_worker->helpersChanged();
}

#endif
