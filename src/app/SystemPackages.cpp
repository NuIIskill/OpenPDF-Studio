#include "app/SystemPackages.hpp"

#include <QFile>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>

#ifdef Q_OS_LINUX
#  include <QDBusConnection>
#  include <QDBusConnectionInterface>
#  include <QDBusMessage>
#  include <QDBusObjectPath>
#  include <QDBusPendingCallWatcher>
#  include <QDBusPendingReply>
#endif

namespace {

const QString kService     = QStringLiteral("org.freedesktop.PackageKit");
const QString kTransaction = QStringLiteral("org.freedesktop.PackageKit.Transaction");

// PackageKit's enums, as bit positions where it takes a bitfield.
constexpr quint64 kFilterInstalled    = 1ull << 2;
constexpr quint64 kFilterNewest       = 1ull << 16;
constexpr quint64 kFlagOnlyTrusted    = 1ull << 1;
constexpr uint    kInfoInstalled      = 1;
constexpr uint    kExitSuccess        = 1;

// How well a package id's architecture fits this machine; 0 is foreign.
int archRank(const QString &id)
{
    const QString arch = id.section(u';', 2, 2);
    if (arch == QLatin1String("noarch") || arch == QLatin1String("all")
            || arch == QLatin1String("any"))
        return 1;
    const QString cpu = QSysInfo::currentCpuArchitecture();
    if (arch == cpu) return 2;
    if (cpu == QLatin1String("x86_64") && arch == QLatin1String("amd64")) return 2;
    if (cpu == QLatin1String("arm64") && (arch == QLatin1String("aarch64")
                                          || arch == QLatin1String("arm64")))
        return 2;
    return 0;
}

}

SystemPackages::Family SystemPackages::family()
{
    static const Family known = [] {
        QFile file(QStringLiteral("/etc/os-release"));
        if (!file.open(QIODevice::ReadOnly)) return Family::Other;
        QStringList ids;
        for (const QByteArray &line : file.readAll().split('\n')) {
            if (!line.startsWith("ID=") && !line.startsWith("ID_LIKE=")) continue;
            const QString value = QString::fromUtf8(line.mid(line.indexOf('=') + 1))
                                      .remove(u'"').remove(u'\'');
            ids << value.split(u' ', Qt::SkipEmptyParts);
        }
        for (const QString &id : std::as_const(ids)) {
            if (id == QLatin1String("fedora") || id == QLatin1String("rhel")
                    || id == QLatin1String("centos"))
                return Family::Fedora;
            if (id == QLatin1String("debian") || id == QLatin1String("ubuntu"))
                return Family::Debian;
            if (id == QLatin1String("arch"))
                return Family::Arch;
        }
        return Family::Other;
    }();
    return known;
}

QStringList SystemPackages::namesFor(const Names &names)
{
    switch (family()) {
    case Family::Fedora: return names.fedora;
    case Family::Debian: return names.debian;
    case Family::Arch:   return names.arch;
    case Family::Other:  break;
    }
    return {};
}

QString SystemPackages::command(const QStringList &packages, bool install)
{
    if (packages.isEmpty()) return {};
    QString tool;
    switch (family()) {
    case Family::Fedora:
        tool = install ? QStringLiteral("sudo dnf install") : QStringLiteral("sudo dnf remove");
        break;
    case Family::Debian:
        tool = install ? QStringLiteral("sudo apt install") : QStringLiteral("sudo apt remove");
        break;
    case Family::Arch:
        tool = install ? QStringLiteral("sudo pacman -S") : QStringLiteral("sudo pacman -R");
        break;
    case Family::Other:
        return {};
    }
    return tool + u' ' + packages.join(u' ');
}

bool SystemPackages::available()
{
#ifdef Q_OS_LINUX
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) return false;
    if (bus.interface()->isServiceRegistered(kService)) return true;
    // Not running yet, which is the usual case: D-Bus starts it on the first call.
    const QDBusMessage reply = bus.call(QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ListActivatableNames")));
    return reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()
        && reply.arguments().first().toStringList().contains(kService);
#else
    return false;
#endif
}

bool SystemPackages::canChange()
{
    return family() != Family::Other
        && (available() || !QStandardPaths::findExecutable(QStringLiteral("pkexec")).isEmpty());
}

QStringList SystemPackages::dependents(const QStringList &packages)
{
    if (packages.isEmpty()) return {};
    QString program;
    QStringList arguments;
    switch (family()) {
    case Family::Fedora:
        program   = QStringLiteral("dnf");
        arguments = { QStringLiteral("repoquery"), QStringLiteral("--installed"),
                      QStringLiteral("--whatrequires"), packages.join(u','),
                      QStringLiteral("--qf"), QStringLiteral("%{name}\\n") };
        break;
    case Family::Debian:
        program   = QStringLiteral("apt-cache");
        arguments = { QStringLiteral("rdepends"), QStringLiteral("--installed"),
                      QStringLiteral("--no-recommends"), QStringLiteral("--no-suggests"),
                      QStringLiteral("--no-enhances"), QStringLiteral("--no-conflicts"),
                      QStringLiteral("--no-breaks"), QStringLiteral("--no-replaces") };
        arguments << packages;
        break;
    case Family::Arch:
        program   = QStringLiteral("pacman");
        arguments = QStringList { QStringLiteral("-Qi") } + packages;
        break;
    case Family::Other:
        return {};
    }

    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(15000)) return {};
    const QString out = QString::fromLocal8Bit(process.readAllStandardOutput());

    QStringList names;
    if (family() == Family::Arch) {
        // "Required By     : obs-studio  vlc", or "None".
        static const QRegularExpression line(QStringLiteral("^Required By\\s*:\\s*(.*)$"),
                                             QRegularExpression::MultilineOption);
        for (auto it = line.globalMatch(out); it.hasNext();)
            names << it.next().captured(1).split(u' ', Qt::SkipEmptyParts);
        names.removeAll(QStringLiteral("None"));
    } else {
        for (const QString &raw : out.split(u'\n', Qt::SkipEmptyParts)) {
            // apt-cache prints the package and a heading before its indented list.
            if (family() == Family::Debian && !raw.startsWith(u' ')) continue;
            QString name = raw.trimmed();
            if (name.startsWith(u'|')) name.remove(0, 1);
            names << name.section(u':', 0, 0);
        }
    }

    QSet<QString> seen(packages.cbegin(), packages.cend());
    QStringList found;
    for (const QString &name : std::as_const(names))
        if (!name.isEmpty() && !seen.contains(name)) {
            seen.insert(name);
            found << name;
        }
    return found;
}

SystemPackages::SystemPackages(QObject *parent)
    : QObject(parent)
{
}

void SystemPackages::install(const QStringList &packages)
{
    if (isBusy()) return;
    m_install        = true;
    m_withDependents = false;
    m_names          = packages;
    start();
}

void SystemPackages::remove(const QStringList &packages, bool withDependents)
{
    if (isBusy()) return;
    m_install        = false;
    m_withDependents = withDependents;
    m_names          = packages;
    start();
}

void SystemPackages::start()
{
    if (available())
        begin(Step::Resolve);
    else
        elevate();
}

void SystemPackages::begin(Step step)
{
#ifdef Q_OS_LINUX
    m_step     = step;
    m_error.clear();
    m_starting = true;
    // The first call may start the daemon, which takes a moment.
    const QDBusPendingCall call = QDBusConnection::systemBus().asyncCall(
        QDBusMessage::createMethodCall(kService, QStringLiteral("/org/freedesktop/PackageKit"),
                                       kService, QStringLiteral("CreateTransaction")));
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        m_starting = false;
        const QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError()) {
            elevate();
            return;
        }
        m_transaction = reply.value().path();
        listen(true);
        run();
    });
#else
    Q_UNUSED(step);
    done(false, tr("Packages are installed by the system's package manager."));
#endif
}

void SystemPackages::run()
{
#ifdef Q_OS_LINUX
    QDBusConnection bus = QDBusConnection::systemBus();
    const auto method = [this](const QString &name) {
        return QDBusMessage::createMethodCall(kService, m_transaction, kTransaction, name);
    };

    QDBusMessage hints = method(QStringLiteral("SetHints"));
    hints << QStringList { QStringLiteral("interactive=true"),
                           QStringLiteral("locale=%1.UTF-8").arg(QLocale().name()) };
    bus.call(hints);

    QDBusMessage call;
    if (m_step == Step::Resolve) {
        m_found.clear();
        call = method(QStringLiteral("Resolve"));
        call << QVariant::fromValue<quint64>(m_install ? kFilterNewest : kFilterInstalled)
             << m_names;
    } else {
        QStringList ids;
        for (auto it = m_found.cbegin(); it != m_found.cend(); ++it)
            if (it->installed != m_install) ids << it->id;
        call = method(m_install ? QStringLiteral("InstallPackages")
                                : QStringLiteral("RemovePackages"));
        call << QVariant::fromValue<quint64>(kFlagOnlyTrusted) << ids;
        // Dependent programs only once the user agreed to lose them.
        if (!m_install) call << m_withDependents << false;
    }
    const QDBusMessage reply = bus.call(call);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        listen(false);
        done(false, reply.errorMessage());
    }
#endif
}

void SystemPackages::elevate()
{
    // Without PackageKit the package manager itself runs as root, behind the
    // desktop's password prompt.
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    QStringList arguments;
    switch (family()) {
    case Family::Fedora:
        arguments = { QStringLiteral("dnf"), QStringLiteral("-y"),
                      m_install ? QStringLiteral("install") : QStringLiteral("remove") };
        break;
    case Family::Debian:
        arguments = { QStringLiteral("apt-get"), QStringLiteral("-y"),
                      m_install ? QStringLiteral("install") : QStringLiteral("remove") };
        break;
    case Family::Arch:
        arguments = { QStringLiteral("pacman"), QStringLiteral("--noconfirm"),
                      m_install ? QStringLiteral("-S")
                                : m_withDependents ? QStringLiteral("-Rc") : QStringLiteral("-R") };
        break;
    case Family::Other:
        break;
    }
    if (pkexec.isEmpty() || arguments.isEmpty()) {
        done(false, tr("Packages are installed by the system's package manager."));
        return;
    }
    arguments << m_names;

    auto *process = new QProcess(this);
    m_starting = true;
    connect(process, &QProcess::finished, this,
            [this, process](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        m_starting = false;
        if (status == QProcess::NormalExit && code == 0) {
            done(true, {});
            return;
        }
        // pkexec answers 126 when the password dialog was dismissed.
        if (code == 126 || code == 127) {
            done(false, tr("No permission was given."));
            return;
        }
        const QStringList lines = QString::fromLocal8Bit(process->readAllStandardError())
                                      .split(u'\n', Qt::SkipEmptyParts);
        done(false, lines.isEmpty() ? tr("The package manager stopped.") : lines.last());
    });
    process->start(pkexec, arguments);
}

void SystemPackages::listen(bool on)
{
#ifdef Q_OS_LINUX
    QDBusConnection bus = QDBusConnection::systemBus();
    const auto hook = [&](const char *signal, const char *slot) {
        if (on)
            bus.connect(kService, m_transaction, kTransaction, QLatin1String(signal), this, slot);
        else
            bus.disconnect(kService, m_transaction, kTransaction, QLatin1String(signal), this, slot);
    };
    hook("Package",   SLOT(onPackage(uint,QString,QString)));
    hook("ErrorCode", SLOT(onErrorCode(uint,QString)));
    hook("Finished",  SLOT(onFinished(uint,uint)));
#else
    Q_UNUSED(on);
#endif
}

void SystemPackages::onPackage(uint info, const QString &id, const QString &)
{
    if (m_step != Step::Resolve) return;
    const int rank = archRank(id);
    if (rank == 0) return;
    const QString name = id.section(u';', 0, 0);
    Found &found = m_found[name];
    const bool installed = info == kInfoInstalled;
    // An installed package counts over one only available.
    if (found.id.isEmpty() || installed > found.installed
            || (installed == found.installed && rank > found.rank))
        found = { id, rank, installed };
}

void SystemPackages::onErrorCode(uint, const QString &details)
{
    m_error = details;
}

void SystemPackages::onFinished(uint exit, uint)
{
    listen(false);
    m_transaction.clear();
    if (exit != kExitSuccess) {
        done(false, m_error.isEmpty() ? tr("The package manager stopped.") : m_error);
        return;
    }
    if (m_step == Step::Change) {
        done(true, {});
        return;
    }

    if (m_install) {
        QStringList missing;
        for (const QString &name : std::as_const(m_names))
            if (!m_found.contains(name)) missing << name;
        if (!missing.isEmpty()) {
            done(false, tr("Not found in the package sources: %1").arg(missing.join(u", ")));
            return;
        }
    }
    for (const Found &found : std::as_const(m_found))
        if (found.installed != m_install) {
            begin(Step::Change);
            return;
        }
    done(true, {});
}

void SystemPackages::done(bool ok, const QString &error)
{
    Q_EMIT finished(ok, error);
}
