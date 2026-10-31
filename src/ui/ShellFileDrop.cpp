#include "ui/ShellFileDrop.hpp"

#ifdef Q_OS_WIN

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDir>
#include <QDropEvent>
#include <QMimeData>
#include <QPointer>
#include <QUrl>
#include <QWidget>
#include <QWindow>

#include <string>
#include <vector>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace {

constexpr UINT kCopyGlobalData = 0x0049;

bool aboveMediumIntegrity()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD size = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    bool above = false;
    if (size && GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), size, &size)) {
        const PSID sid = reinterpret_cast<const TOKEN_MANDATORY_LABEL *>(buffer.data())->Label.Sid;
        const DWORD level = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
        above = level > SECURITY_MANDATORY_MEDIUM_RID;
    }
    CloseHandle(token);
    return above;
}

// Goes the way an OLE drop goes: enter, move and drop on the QWindow, which
// hands them to the widget under the point.
bool dropOnWindow(QWidget *window, const QPointF &position, const QStringList &paths)
{
    QWindow *handle = window->windowHandle();
    if (!handle) return false;

    QList<QUrl> urls;
    for (const QString &path : paths) urls << QUrl::fromLocalFile(path);
    QMimeData mime;
    mime.setUrls(urls);

    QDragEnterEvent enter(position.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(handle, &enter);
    QDragMoveEvent move(position.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(handle, &move);
    if (!move.isAccepted()) {
        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(handle, &leave);
        return false;
    }
    QDropEvent drop(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(handle, &drop);
    return drop.isAccepted();
}

class DropFilter : public QAbstractNativeEventFilter
{
public:
    DropFilter(QWidget *window, HWND hwnd, std::function<void(const QStringList &)> open)
        : m_window(window), m_hwnd(hwnd), m_open(std::move(open)) {}

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
        POINT point {};
        DragQueryPoint(drop, &point);
        DragFinish(drop);

        // Queued: the shell waits until this message returns.
        if (!paths.isEmpty() && m_window) {
            const qreal ratio = m_window->devicePixelRatioF();
            const QPointF position(point.x / ratio, point.y / ratio);
            QMetaObject::invokeMethod(qApp, [window = m_window, open = m_open, position, paths] {
                if (!window || !dropOnWindow(window, position, paths)) open(paths);
            }, Qt::QueuedConnection);
        }
        return true;
    }

private:
    QPointer<QWidget> m_window;
    HWND m_hwnd;
    std::function<void(const QStringList &)> m_open;
};

}

void ShellFileDrop::enable(QWidget *window, std::function<void(const QStringList &)> open)
{
    // The shell only falls back to WM_DROPFILES where no OLE drop target is registered.
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    DragAcceptFiles(hwnd, TRUE);

    // Windows refuses OLE drops from a process of lower integrity, Explorer's
    // included, so an elevated window takes files only by these messages.
    if (aboveMediumIntegrity()) {
        for (const UINT message : { UINT(WM_DROPFILES), UINT(WM_COPYDATA), kCopyGlobalData })
            ChangeWindowMessageFilterEx(hwnd, message, MSGFLT_ALLOW, nullptr);
        RevokeDragDrop(hwnd);
    }

    auto *filter = new DropFilter(window, hwnd, std::move(open));
    QCoreApplication::instance()->installNativeEventFilter(filter);
    QObject::connect(window, &QObject::destroyed, [filter] { delete filter; });
}

#else

void ShellFileDrop::enable(QWidget *, std::function<void(const QStringList &)>) {}

#endif
