#include "engine/document/DocumentWorker.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/document/PdfiumLock.hpp"
#include "engine/document/RenderPool.hpp"
#include "app/PdfPwStore.hpp"
#include "engine/edit/EditSession.hpp"
#include "engine/ocr/OcrSearch.hpp"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QStringList>
#include <QThread>

#include <algorithm>
#include <cstdlib>

#ifdef Q_OS_LINUX
#  include <sys/resource.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#endif

namespace {

QList<PdfBackend::ScanLine> scanLines(const QList<OcrEngine::Line> &read)
{
    // One box for each character, as PDFium counts them: a space between words has none.
    QList<PdfBackend::ScanLine> lines;
    for (const OcrEngine::Line &words : read) {
        PdfBackend::ScanLine line;
        for (const OcrEngine::Word &word : words) {
            if (!line.text.isEmpty()) {
                line.text += u' ';
                line.boxes.append(QRectF());
            }
            for (qsizetype i = 0; i < word.text.size(); ++i) {
                line.text += word.text.at(i);
                if (!word.text.at(i).isLowSurrogate()) line.boxes.append(word.chars.value(i));
            }
        }
        if (!line.boxes.isEmpty()) lines.append(line);
    }
    return lines;
}

}

DocumentWorker::DocumentWorker(PdfBackend *backend)
    : m_backend(backend)
{
    m_ocr = std::make_unique<OcrSearch>(
        [this] {
            { std::lock_guard lock(m_mutex); }
            m_wake.notify_all();
        },
        [this](int page, quint64 epoch, bool ready, const QList<OcrEngine::Line> &read) {
            // The text is set on this thread: the fonts of its layer outlive the reader.
            QMetaObject::invokeMethod(this, [this, page, epoch, ready, read] {
                if (!m_ocr->current(epoch)) return;
                if (ready) m_backend->setScanText(page, scanLines(read));
                m_ocr->done(page, epoch, ready);
                scanRead(page);
            }, Qt::QueuedConnection);
        },
        [this](int page, quint64 epoch, int percent) {
            QMetaObject::invokeMethod(this, [this, page, epoch, percent] {
                if (m_ocr->current(epoch)) Q_EMIT scanProgress(page, percent);
            }, Qt::QueuedConnection);
        });
    m_thread = QThread::create([this] {
#ifdef Q_OS_LINUX
        // Linux gives each thread its own niceness; on few cores the window
        // always gets the processor first.
        setpriority(PRIO_PROCESS, pid_t(syscall(SYS_gettid)), 10);
#endif
        run();
    });
    m_thread->start(QThread::LowPriority);
}

DocumentWorker::~DocumentWorker()
{
    delete m_pool;
    m_pool = nullptr;
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
        m_cancelRender = true;
        m_cancelCopy   = true;
    }
    m_wake.notify_all();
    m_thread->wait();
    delete m_thread;
}

void DocumentWorker::setRenders(const QList<Render> &renders)
{
    {
        std::lock_guard lock(m_mutex);
        m_renders = renders;
        // What is already being rendered stays running if it is still wanted.
        const auto keep = [this](const Render &running) {
            const auto it = std::find_if(m_renders.begin(), m_renders.end(), [&](const Render &r) {
                return r.page == running.page && r.zoom == running.zoom
                    && r.version == running.version && r.area == running.area;
            });
            if (it == m_renders.end()) return false;
            m_renders.erase(it);
            return true;
        };
        // A render marked to finish is still useful once started.
        if (m_runningRender && !keep(*m_runningRender) && !m_runningRender->finish)
            m_cancelRender = true;
        for (auto it = m_helperJobs.begin(); it != m_helperJobs.end(); ++it) {
            if (it->cancelled || keep(it->render) || it->render.finish) continue;
            it->cancelled = true;
            m_pool->requestCancel(it.key());
        }
    }
    m_wake.notify_all();
    if (m_pool) m_pool->requestDispatch();
}

void DocumentWorker::readAnnotations(const QList<int> &pages)
{
    {
        std::lock_guard lock(m_mutex);
        m_annotationPages = pages;
    }
    m_wake.notify_all();
}

void DocumentWorker::writeCopy(const WriteGrant &output, std::shared_ptr<const EditSession> session)
{
    {
        std::lock_guard lock(m_mutex);
        m_copies.append(Copy { output, std::move(session) });
    }
    m_wake.notify_all();
}

void DocumentWorker::stop()
{
    Q_ASSERT(!PdfiumLock::heldByThisThread());
    QStringList cancelled;
    {
        std::unique_lock lock(m_mutex);
        ++m_generation;
        m_renders.clear();
        m_annotationPages.clear();
        m_search = {};
        m_scanWait = {};
        m_scanPages.clear();
        m_helperPages.clear();
        for (const Copy &copy : std::as_const(m_copies)) cancelled.append(copy.output.path());
        m_copies.clear();
        if (!m_runningCopy.isEmpty()) cancelled.append(m_runningCopy);
        m_cancelRender = true;
        m_cancelCopy   = true;
        for (auto it = m_helperJobs.begin(); it != m_helperJobs.end(); ++it) {
            if (it->cancelled) continue;
            it->cancelled = true;
            m_pool->requestCancel(it.key());
        }
        m_idle.wait(lock, [this] { return !m_busy; });
    }
    m_ocr->reset();
    if (m_pool) m_pool->closeDocument();
    m_helpersOpen = false;
    for (const QString &output : std::as_const(cancelled)) postCopyFailed(output);
    QMetaObject::invokeMethod(this, [this] { Q_EMIT stopped(); }, Qt::QueuedConnection);
}

void DocumentWorker::findText(int search, const QString &needle)
{
    m_scanWait = { search, needle, {}, false };
    m_ocr->retry();
    {
        std::lock_guard lock(m_mutex);
        m_search = { search, needle, 0 };
    }
    m_wake.notify_all();
}

void DocumentWorker::readScan(int page)
{
    m_ocr->retry();
    {
        std::lock_guard lock(m_mutex);
        m_scanPages.removeAll(page);
        m_scanPages.prepend(page);
    }
    m_wake.notify_all();
}

void DocumentWorker::setDocument(const QString &path)
{
    // A small document stays in this process, where nothing can go wrong
    // between processes; helpers join once it shows to be expensive.
    constexpr qint64 kLargeFile = 32ll * 1024 * 1024;
    m_documentPath = path;
    m_helpersOpen  = false;
    m_slowRenders  = 0;
    if (QFileInfo(path).size() >= kLargeFile) useHelpers();
}

void DocumentWorker::useHelpers()
{
    if (m_helpersOpen || m_documentPath.isEmpty() || !RenderPool::available()) return;
    if (!m_pool) {
        RenderPool *pool = new RenderPool(this);
        std::lock_guard lock(m_mutex);
        m_pool = pool;
    }
    m_helpersOpen = true;
    m_pool->requestOpen(m_documentPath, PdfPwStore::get(m_documentPath));
}

bool DocumentWorker::forHelpers(const Render &render) const
{
    // Unedited pages render the same in a helper, which has the file open too.
    return !render.session && m_pool && m_pool->liveHelpers() > 0;
}

bool DocumentWorker::hasOwnWork() const
{
    if (m_quit || !m_copies.isEmpty() || !m_annotationPages.isEmpty()) return true;
    if (searchReady() || scanReady()) return true;
    return std::any_of(m_renders.cbegin(), m_renders.cend(),
                       [this](const Render &r) { return !forHelpers(r); });
}

std::optional<DocumentWorker::Render> DocumentWorker::takeForHelper(int helper)
{
    std::lock_guard lock(m_mutex);
    // A helper keeps its last pages loaded with their images decoded; among the
    // next few renders it takes one of those pages first.
    constexpr int kLookAhead = 6;
    const QList<int> &recent = m_helperPages[helper];
    auto it = m_renders.end();
    int seen = 0;
    for (auto r = m_renders.begin(); r != m_renders.end() && seen < kLookAhead; ++r) {
        if (r->session) continue;
        if (it == m_renders.end()) it = r;
        if (recent.contains(r->page)) { it = r; break; }
        ++seen;
    }
    if (it == m_renders.end()) return std::nullopt;
    QList<int> &pages = m_helperPages[helper];
    pages.removeAll(it->page);
    pages.prepend(it->page);
    if (pages.size() > 2) pages.removeLast();
    const Render render = *it;
    m_renders.erase(it);
    m_helperJobs.insert(helper, { render, m_generation.load(), false });
    return render;
}

void DocumentWorker::helperDone(int helper, const QImage &image, int milliseconds)
{
    HelperJob job;
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_helperJobs.find(helper);
        if (it == m_helperJobs.end()) return;
        job = *it;
        m_helperJobs.erase(it);
    }
    if (image.isNull() || job.cancelled || job.generation != m_generation) return;
    const Render render = job.render;
    post(job.generation, [this, render, image, milliseconds] {
        Q_EMIT pageRendered(render.page, render.zoom, render.version, render.area, image,
                            milliseconds);
    });
}

void DocumentWorker::helpersChanged()
{
    m_wake.notify_all();
}

void DocumentWorker::run()
{
    for (;;) {
        std::optional<Render> render;
        std::optional<Copy>   copy;
        Search   search;
        bool     searchLast     = false;
        int      annotationPage = -1;
        int      scanPage       = -1;
        quint64  generation     = 0;
        {
            std::unique_lock lock(m_mutex);
            m_busy = false;
            m_runningRender.reset();
            m_runningCopy.clear();
            m_idle.notify_all();
            m_wake.wait(lock, [this] { return hasOwnWork(); });
            if (m_quit) return;
            m_busy     = true;
            generation = m_generation;

            const auto own = std::find_if(m_renders.begin(), m_renders.end(),
                                          [this](const Render &r) { return !forHelpers(r); });
            // A search takes turns with renders, so neither waits for the other.
            const bool searching = searchReady();
            m_searchTurn = searching && (!m_searchTurn || own == m_renders.end());
            if (m_searchTurn) {
                search     = m_search;
                searchLast = ++m_search.next >= m_backend->pageCount();
            } else if (own != m_renders.end()) {
                render = *own;
                m_renders.erase(own);
                m_runningRender = render;
                m_cancelRender  = false;
            } else if (!m_copies.isEmpty()) {
                copy = m_copies.takeFirst();
                m_runningCopy = copy->output.path();
                m_cancelCopy  = false;
            } else if (scanReady()) {
                scanPage = m_scanPages.takeFirst();
            } else {
                const int focus = m_focusPage;
                qsizetype best = 0;
                for (qsizetype i = 1; i < m_annotationPages.size(); ++i)
                    if (std::abs(m_annotationPages.at(i) - focus)
                            < std::abs(m_annotationPages.at(best) - focus))
                        best = i;
                annotationPage = m_annotationPages.takeAt(best);
            }
        }

        if (render)      runRender(*render, generation);
        else if (search.id) runSearch(search.id, search.needle, search.next, searchLast, generation);
        else if (copy)   runCopy(*copy, generation);
        else if (scanPage >= 0) runScan(scanPage, generation);
        else             runAnnotations(annotationPage, generation);
    }
}

void DocumentWorker::runRender(const Render &render, quint64 generation)
{
    QElapsedTimer clock;
    clock.start();
    // An edited page's version names its state, so its renders can share one load.
    const QImage image = render.textOnly && render.session
        ? m_backend->renderEditText(render.page, render.scale, render.session->snapshotEdits(),
                                    render.area)
        : m_backend->renderPageInBackground(
              render.page, render.scale, render.session.get(),
              [this] { return m_cancelRender.load(); }, render.area,
              render.session ? quint64(render.version) : 0);
    if (image.isNull() || m_cancelRender) return;
    const int ms = int(clock.elapsed());
    // Pages this slow make the document worth rendering in parallel.
    constexpr int kSlowRenderMs = 120;
    if (ms >= kSlowRenderMs && ++m_slowRenders == 2)
        QMetaObject::invokeMethod(this, [this] { useHelpers(); }, Qt::QueuedConnection);
    post(generation, [this, render, image, ms] {
        Q_EMIT pageRendered(render.page, render.zoom, render.version, render.area, image, ms);
    });
}

void DocumentWorker::runCopy(const Copy &copy, quint64 generation)
{
    static const EditSession empty;
    const bool ok = m_backend->saveWithEdits(copy.output, copy.session ? *copy.session : empty,
                                             [this] { return m_cancelCopy.load(); });
    if (m_cancelCopy) return;
    post(generation, [this, output = copy.output.path(), ok] { Q_EMIT copyWritten(output, ok); });
}

void DocumentWorker::runAnnotations(int page, quint64 generation)
{
    const QList<PdfBackend::Link> links = m_backend->pageLinks(page);
    const QList<PdfBackend::Note> notes = m_backend->pageNotes(page);
    post(generation, [this, page, links, notes] {
        Q_EMIT annotationsRead(page, links, notes);
    });
}

bool DocumentWorker::searchReady() const
{
    // Scanned pages wait their turn with OCR rather than piling up as images.
    return !m_search.needle.isEmpty() && m_search.next < m_backend->pageCount()
        && m_ocr->queued() < OcrSearch::threads();
}

bool DocumentWorker::scanReady() const
{
    return !m_scanPages.isEmpty() && m_ocr->queued() < OcrSearch::threads();
}

bool DocumentWorker::startScan(int page, quint64 generation)
{
    if (m_ocr->reading(page)) return true;
    const QSizeF size = m_backend->pageSizePts(page);
    const qreal scale = OcrSearch::scaleFor(size);
    // What the view waits for comes first; an interrupted page is taken up again.
    bool yielded = false;
    const QImage image = m_backend->renderPageInBackground(
        page, scale, nullptr, [this, generation, &yielded] {
            if (generation != m_generation) return true;
            std::lock_guard lock(m_mutex);
            yielded = std::any_of(m_renders.cbegin(), m_renders.cend(),
                                  [this](const Render &r) { return !forHelpers(r); });
            return yielded;
        });
    if (yielded) return false;
    if (!image.isNull())
        m_ocr->read(page, image.convertToFormat(QImage::Format_Grayscale8), size, scale);
    return true;
}

void DocumentWorker::runScan(int page, quint64 generation)
{
    const bool wanted = m_ocr->available() && page < m_backend->pageCount()
                     && !m_backend->hasSelectableText(page) && !m_backend->hasScanText(page);
    if (wanted && !startScan(page, generation)) {
        std::lock_guard lock(m_mutex);
        if (generation == m_generation && !m_scanPages.contains(page)) m_scanPages.prepend(page);
        return;
    }
    // A page read on is announced once OCR hands its text over.
    if (!wanted || !m_ocr->reading(page))
        post(generation, [this, page] { Q_EMIT scanTextRead(page); });
}

void DocumentWorker::runSearch(int search, const QString &needle, int page, bool last,
                               quint64 generation)
{
    const QList<PdfBackend::TextMatch> matches = m_backend->findTextOnPage(page, needle);
    // A scan has no text of its own; OCR reads it once, its own threads aside,
    // and the normal search reads what it found.
    if (matches.isEmpty() && m_ocr->available() && !m_backend->hasSelectableText(page)
            && !m_backend->hasScanText(page)) {
        if (!startScan(page, generation)) {
            std::lock_guard lock(m_mutex);
            if (m_search.id == search) m_search.next = std::min(m_search.next, page);
            return;
        }
        post(generation, [this, search, page, last] { waitForScan(search, page, last); });
        return;
    }
    const QList<PdfBackend::TextMatch> found = matches.isEmpty() && m_backend->hasScanText(page)
        ? m_backend->findScanText(page, needle) : matches;
    post(generation, [this, search, page, found, last] {
        pageDone(search, page, found, last);
    });
}

void DocumentWorker::pageDone(int search, int page, const QList<PdfBackend::TextMatch> &matches,
                              bool last)
{
    if (!matches.isEmpty()) Q_EMIT textFound(search, page, matches);
    Q_EMIT pageSearched(search, page);
    if (search != m_scanWait.search) return;
    m_scanWait.scanned = m_scanWait.scanned || last;
    if (!m_scanWait.scanned || !m_scanWait.pages.isEmpty()) return;
    m_scanWait = {};
    Q_EMIT searchDone(search);
}

void DocumentWorker::waitForScan(int search, int page, bool last)
{
    if (search != m_scanWait.search) return;
    if (m_ocr->reading(page)) {
        m_scanWait.pages.append(page);
        m_scanWait.scanned = m_scanWait.scanned || last;
        return;
    }
    pageDone(search, page, m_backend->findScanText(page, m_scanWait.needle), last);
}

void DocumentWorker::scanRead(int page)
{
    Q_EMIT scanTextRead(page);
    if (!m_scanWait.pages.removeOne(page)) return;
    waitForScan(m_scanWait.search, page, false);
}

void DocumentWorker::post(quint64 generation, std::function<void()> emitter)
{
    QMetaObject::invokeMethod(this, [this, generation, emitter = std::move(emitter)] {
        if (generation == m_generation) emitter();
    }, Qt::QueuedConnection);
}

void DocumentWorker::postCopyFailed(const QString &output)
{
    QMetaObject::invokeMethod(this, [this, output] { Q_EMIT copyWritten(output, false); },
                              Qt::QueuedConnection);
}

#endif
