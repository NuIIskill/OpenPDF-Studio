#include "app/Elevation.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QTimer>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shellapi.h>
#endif

void Elevation::runSelf(const QStringList &arguments, std::function<void(Result)> done)
{
#ifdef Q_OS_WIN
    QStringList quoted;
    for (const QString &argument : arguments)
        quoted << u'"' + argument + u'"';
    const std::wstring file =
        QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
    const std::wstring parameters = quoted.join(u' ').toStdWString();

    SHELLEXECUTEINFOW info {};
    info.cbSize       = sizeof(info);
    info.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb       = L"runas";
    info.lpFile       = file.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow        = SW_HIDE;
    if (!ShellExecuteExW(&info) || !info.hProcess) {
        done(GetLastError() == ERROR_CANCELLED ? Result::Declined : Result::Failed);
        return;
    }

    // Waited for without blocking the window, which keeps painting meanwhile.
    HANDLE process = info.hProcess;
    auto *timer = new QTimer(QCoreApplication::instance());
    timer->setInterval(100);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, process, done]() {
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) return;
        DWORD code = 1;
        GetExitCodeProcess(process, &code);
        CloseHandle(process);
        timer->stop();
        timer->deleteLater();
        done(code == 0 ? Result::Done : Result::Failed);
    });
    timer->start();
#else
    Q_UNUSED(arguments);
    done(Result::Failed);
#endif
}
