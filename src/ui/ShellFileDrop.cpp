#include "ui/ShellFileDrop.hpp"

#ifdef Q_OS_WIN

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDir>
#include <QWidget>

#include <string>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace {

class DropFilter : public QAbstractNativeEventFilter
{
public:
    DropFilter(HWND hwnd, std::function<void(const QStringList &)> open)
        : m_hwnd(hwnd), m_open(std::move(open)) {}

    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *) override
    {
        if (type != "windows_generic_MSG") return false;
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message != WM_DROPFILES || msg->hwnd != m_hwnd) return false;

        const auto drop = reinterpret_cast<HDROP>(msg->wParam);
        QStringList paths;
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(length + 1, L'\0');
            if (DragQueryFileW(drop, i, path.data(), length + 1) == length)
                paths << QDir::fromNativeSeparators(QString::fromWCharArray(path.data(), int(length)));
        }
        DragFinish(drop);

        // Queued: the shell waits until this message returns.
        if (!paths.isEmpty())
            QMetaObject::invokeMethod(qApp, [open = m_open, paths] { open(paths); },
                                      Qt::QueuedConnection);
        return true;
    }

private:
    HWND m_hwnd;
    std::function<void(const QStringList &)> m_open;
};

}

void ShellFileDrop::enable(QWidget *window, std::function<void(const QStringList &)> open)
{
    // The shell only falls back to WM_DROPFILES where no OLE drop target is registered.
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    DragAcceptFiles(hwnd, TRUE);
    auto *filter = new DropFilter(hwnd, std::move(open));
    QCoreApplication::instance()->installNativeEventFilter(filter);
    QObject::connect(window, &QObject::destroyed, [filter] { delete filter; });
}

#else

void ShellFileDrop::enable(QWidget *, std::function<void(const QStringList &)>) {}

#endif
