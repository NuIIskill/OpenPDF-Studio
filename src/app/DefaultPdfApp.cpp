#include "app/DefaultPdfApp.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <iterator>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  include <shlobj.h>
#  include <shlwapi.h>
#endif

namespace DefaultPdfApp {

namespace {

void answerLater(QObject *context, std::function<void(State)> done, State state)
{
    QTimer::singleShot(0, context, [done = std::move(done), state]() { done(state); });
}

#ifdef Q_OS_WIN

constexpr auto kProgId  = "OpenPDFStudio.Document";
constexpr auto kAppName = "OpenPDF Studio";

State currentState()
{
    wchar_t exe[MAX_PATH * 4];
    DWORD size = DWORD(std::size(exe));
    if (FAILED(AssocQueryStringW(ASSOCF_NOTRUNCATE, ASSOCSTR_EXECUTABLE,
                                 L".pdf", L"open", exe, &size)))
        return State::NotDefault;

    const QString handler = QFileInfo(QString::fromWCharArray(exe)).canonicalFilePath();
    const QString own = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    return handler.compare(own, Qt::CaseInsensitive) == 0 ? State::Default
                                                         : State::NotDefault;
}

#else

constexpr auto kDesktopId = "openpdf-studio.desktop";
constexpr auto kPdfMime   = "application/pdf";

QString xdgMime()
{
    if (QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                               QLatin1String(kDesktopId)).isEmpty())
        return {};
    return QStandardPaths::findExecutable(QStringLiteral("xdg-mime"));
}

#endif

}

void query(QObject *context, std::function<void(State)> done)
{
#ifdef Q_OS_WIN
    answerLater(context, std::move(done), currentState());
#else
    const QString tool = xdgMime();
    if (tool.isEmpty()) {
        answerLater(context, std::move(done), State::Unknown);
        return;
    }

    auto *proc = new QProcess(context);
    QObject::connect(proc, &QProcess::finished, context,
                     [proc, done](int code, QProcess::ExitStatus status) {
        proc->deleteLater();
        if (status != QProcess::NormalExit || code != 0) {
            done(State::Unknown);
            return;
        }
        const QString current = QString::fromLocal8Bit(proc->readAllStandardOutput()).trimmed();
        done(current == QLatin1String(kDesktopId) ? State::Default : State::NotDefault);
    });
    QObject::connect(proc, &QProcess::errorOccurred, context,
                     [proc, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        proc->deleteLater();
        done(State::Unknown);
    });
    proc->start(tool, {QStringLiteral("query"), QStringLiteral("default"),
                       QLatin1String(kPdfMime)});
#endif
}

bool makeDefault()
{
#ifdef Q_OS_WIN
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString progId = QLatin1String(kProgId);

    QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"),
                      QSettings::NativeFormat);
    classes.setValue(progId + QStringLiteral("/Default"), QStringLiteral("PDF Document"));
    classes.setValue(progId + QStringLiteral("/DefaultIcon/Default"), exe + QStringLiteral(",0"));
    classes.setValue(progId + QStringLiteral("/shell/open/command/Default"),
                     QStringLiteral("\"%1\" \"%2\"").arg(exe, QStringLiteral("%1")));
    classes.setValue(QStringLiteral(".pdf/OpenWithProgids/") + progId, QString());
    classes.sync();

    QSettings software(QStringLiteral("HKEY_CURRENT_USER\\Software"), QSettings::NativeFormat);
    software.setValue(QStringLiteral("OpenPDFStudio/Capabilities/ApplicationName"),
                      QLatin1String(kAppName));
    software.setValue(QStringLiteral("OpenPDFStudio/Capabilities/ApplicationDescription"),
                      QStringLiteral("View and edit PDF documents"));
    software.setValue(QStringLiteral("OpenPDFStudio/Capabilities/FileAssociations/.pdf"),
                      progId);
    software.setValue(QStringLiteral("RegisteredApplications/") + QLatin1String(kAppName),
                      QStringLiteral("Software\\OpenPDFStudio\\Capabilities"));
    software.sync();

    if (classes.status() != QSettings::NoError || software.status() != QSettings::NoError)
        return false;

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    const auto opened = reinterpret_cast<INT_PTR>(ShellExecuteW(
        nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=OpenPDF%20Studio",
        nullptr, nullptr, SW_SHOWNORMAL));
    return opened > 32;
#else
    const QString tool = xdgMime();
    return !tool.isEmpty()
        && QProcess::execute(tool, {QStringLiteral("default"), QLatin1String(kDesktopId),
                                    QLatin1String(kPdfMime)}) == 0;
#endif
}

bool needsSystemSettings()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

}
