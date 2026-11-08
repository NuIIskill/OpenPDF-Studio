#pragma once

#include "engine/ocr/OcrEngine.hpp"

#include <QImage>
#include <QList>
#include <QSet>
#include <QSizeF>

#include <atomic>
#include <functional>
#include <mutex>

class QThread;

/// Reads scanned pages with OCR on background threads.
class OcrSearch
{
public:
    using PageRead = std::function<void(int page, quint64 epoch, bool ready, const QList<OcrEngine::Line> &lines)>;

    using Progress = std::function<void(int page, quint64 epoch, int percent)>;

    OcrSearch(std::function<void()> jobTaken, PageRead pageRead, Progress progress);
    ~OcrSearch();

    OcrSearch(const OcrSearch &)            = delete;
    OcrSearch &operator=(const OcrSearch &) = delete;

    static int threads();
    static qreal scaleFor(const QSizeF &pageSizePts);

    bool available() const;
    bool current(quint64 epoch) const { return m_epoch == epoch; }
    void retry();
    void reset();
    bool reading(int page) const;
    int  queued() const;
    void read(int page, const QImage &image, const QSizeF &pageSizePts, qreal renderScale);
    void done(int page, quint64 epoch, bool ready);

private:
    struct Job {
        int     page { -1 };
        QImage  image;
        QSizeF  pageSize;
        qreal   scale { 1.0 };
        quint64 epoch { 0 };
    };

    void run();

    std::function<void()> m_jobTaken;
    PageRead              m_pageRead;
    Progress              m_progress;

    mutable std::mutex   m_mutex;
    QList<Job>           m_jobs;
    QSet<int>            m_reading;
    QList<QThread *>     m_threads;
    int                  m_running { 0 };
    int                  m_busy    { 0 };
    bool                 m_failed  { false };
    std::atomic<quint64> m_epoch   { 0 };
    std::atomic<bool>    m_quit    { false };
};
