#include "engine/ocr/OcrSearch.hpp"
#include "engine/ocr/OcrEngine.hpp"

#include <QThread>

#include <algorithm>
#include <cmath>
#include <memory>

#ifdef Q_OS_LINUX
#  include <sys/resource.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#endif

OcrSearch::OcrSearch(std::function<void()> jobTaken, PageRead pageRead, Progress progress)
    : m_jobTaken(std::move(jobTaken))
    , m_pageRead(std::move(pageRead))
    , m_progress(std::move(progress))
{
}


OcrSearch::~OcrSearch()
{
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
        m_jobs.clear();
    }
    for (QThread *thread : std::as_const(m_threads)) {
        thread->wait();
        delete thread;
    }
}

int OcrSearch::threads()
{
    // Each reader holds its own Tesseract of about 100 MB.
    return std::clamp(QThread::idealThreadCount() / 4, 1, 3);
}

qreal OcrSearch::scaleFor(const QSizeF &pageSizePts)
{
    // Tesseract reads best at 300 dpi; a poster is read at what stays affordable.
    constexpr qreal kScale     = 300.0 / 72.0;
    constexpr qreal kMaxPixels = 36e6;
    const qreal area = pageSizePts.width() * pageSizePts.height();
    return area > 0.0 ? std::min(kScale, std::sqrt(kMaxPixels / area)) : kScale;
}

bool OcrSearch::available() const
{
#ifdef HAVE_TESSERACT
    std::lock_guard lock(m_mutex);
    return !m_failed;
#else
    return false;
#endif
}

void OcrSearch::retry()
{
    std::lock_guard lock(m_mutex);
    m_failed = false;
}

void OcrSearch::reset()
{
    std::lock_guard lock(m_mutex);
    ++m_epoch;
    m_jobs.clear();
    m_reading.clear();
    m_failed = false;
}

bool OcrSearch::reading(int page) const
{
    std::lock_guard lock(m_mutex);
    return m_reading.contains(page);
}

int OcrSearch::queued() const
{
    std::lock_guard lock(m_mutex);
    return int(m_jobs.size());
}

void OcrSearch::read(int page, const QImage &image, const QSizeF &pageSizePts, qreal renderScale)
{
    std::lock_guard lock(m_mutex);
    if (m_quit || m_reading.contains(page)) return;
    m_jobs.append({ page, image, pageSizePts, renderScale, m_epoch });
    m_reading.insert(page);

    for (auto it = m_threads.begin(); it != m_threads.end();) {
        if (!(*it)->isFinished()) { ++it; continue; }
        delete *it;
        it = m_threads.erase(it);
    }
    // A reader stops once nothing is left to read, and its Tesseract with it.
    if (m_running < threads() && m_running - m_busy < m_jobs.size()) {
        ++m_running;
        QThread *thread = QThread::create([this] { run(); });
        m_threads.append(thread);
        thread->start(QThread::LowPriority);
    }
}

void OcrSearch::done(int page, quint64 epoch, bool ready)
{
    std::lock_guard lock(m_mutex);
    if (epoch != m_epoch) return;
    m_reading.remove(page);
    if (!ready) m_failed = true;
}

void OcrSearch::run()
{
#ifdef Q_OS_LINUX
    // Below the document worker too: a render the view waits for comes first.
    setpriority(PRIO_PROCESS, pid_t(syscall(SYS_gettid)), 15);
#endif
    std::unique_ptr<OcrEngine> engine;
    for (;;) {
        Job job;
        {
            std::lock_guard lock(m_mutex);
            if (m_quit || m_jobs.isEmpty()) {
                --m_running;
                return;
            }
            job = m_jobs.takeFirst();
            ++m_busy;
        }
        m_jobTaken();

        if (!engine) engine = std::make_unique<OcrEngine>();
        const bool ready = engine->isReady();
        QList<OcrEngine::Line> lines;
        if (ready) {
            const auto cancelled = [this, epoch = job.epoch] {
                return m_quit || m_epoch != epoch;
            };
            const auto progress = [this, &job](int percent) { m_progress(job.page, job.epoch, percent); };
            lines = engine->recognizeLines(job.image, job.pageSize, job.scale, true, cancelled, progress);
        }
        job.image = QImage();

        {
            std::lock_guard lock(m_mutex);
            --m_busy;
        }
        // The page counts as read only once done() takes what was read in.
        if (!m_quit && job.epoch == m_epoch) m_pageRead(job.page, job.epoch, ready, lines);
    }
}
