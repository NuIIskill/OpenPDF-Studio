#include "app/SystemMemory.hpp"

#include <QFile>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace SystemMemory {

qint64 availableMb()
{
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX status {};
    status.dwLength = sizeof status;
    return GlobalMemoryStatusEx(&status) ? qint64(status.ullAvailPhys / (1024 * 1024)) : -1;
#elif defined(Q_OS_LINUX)
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (!meminfo.open(QIODevice::ReadOnly)) return -1;
    for (const QByteArray &line : meminfo.readAll().split('\n'))
        if (line.startsWith("MemAvailable:"))
            return line.mid(13).trimmed().split(' ').value(0).toLongLong() / 1024;
    return -1;
#else
    return -1;
#endif
}

qint64 cacheBytes()
{
    // A quarter of what is free when first asked, between 192 MB and 2 GB.
    static const qint64 bytes = [] {
        constexpr qint64 kMb = 1024 * 1024;
        const qint64 freeMb = availableMb();
        if (freeMb < 0) return 512 * kMb;
        return qBound(192 * kMb, freeMb / 4 * kMb, 2048 * kMb);
    }();
    return bytes;
}

}
