#include "engine/document/PdfiumLock.hpp"

#include <QCoreApplication>
#include <QThread>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace {

std::mutex       g_mutex;
std::atomic<int> g_waiting { 0 };
std::atomic<int> g_guiWaiting { 0 };
thread_local int t_depth = 0;

bool onGuiThread()
{
    thread_local const bool gui = QCoreApplication::instance()
        && QThread::currentThread() == QCoreApplication::instance()->thread();
    return gui;
}

void waitWhileGuiWaits()
{
    while (g_guiWaiting.load() > 0)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
}

}

PdfiumLock::PdfiumLock()
{
    if (t_depth++ > 0) return;
    const bool gui = onGuiThread();
    if (!gui) waitWhileGuiWaits();
    if (gui) ++g_guiWaiting;
    ++g_waiting;
    g_mutex.lock();
    --g_waiting;
    if (gui) --g_guiWaiting;
}

PdfiumLock::~PdfiumLock()
{
    if (--t_depth > 0) return;
    g_mutex.unlock();
}

bool PdfiumLock::othersWaiting()
{
    return g_waiting.load() > 0;
}

void PdfiumLock::yieldToOthers()
{
    if (t_depth != 1 || g_waiting.load() == 0) return;
    g_mutex.unlock();
    while (g_waiting.load() > 0)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    if (!onGuiThread()) waitWhileGuiWaits();
    g_mutex.lock();
}

bool PdfiumLock::heldByThisThread()
{
    return t_depth > 0;
}
