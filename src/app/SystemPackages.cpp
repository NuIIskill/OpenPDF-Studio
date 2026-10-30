#include "app/SystemPackages.hpp"

#include <QFile>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

namespace {

// The package manager each family uses; it runs as root through pkexec.
QString tool(SystemPackages::Family family)
{
    switch (family) {
    case SystemPackages::Family::Fedora: return QStandardPaths::findExecutable(QStringLiteral("dnf"));
    case SystemPackages::Family::Debian: return QStandardPaths::findExecutable(QStringLiteral("apt-get"));
    case SystemPackages::Family::Arch:   return QStandardPaths::findExecutable(QStringLiteral("pacman"));
    case SystemPackages::Family::Other:  break;
    }
    return {};
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

bool SystemPackages::canChange()
{
    return !tool(family()).isEmpty()
        && !QStandardPaths::findExecutable(QStringLiteral("pkexec")).isEmpty();
}

QStringList SystemPackages::dependents(const QStringList &packages)
{
    // The package manager is asked what a removal would take along; none of
    // these change anything or need root.
    const QString manager = tool(family());
    if (packages.isEmpty() || manager.isEmpty()) return {};
    QStringList arguments;
    switch (family()) {
    case Family::Fedora:
        arguments = QStringList { QStringLiteral("remove"), QStringLiteral("--assumeno") }
                  + packages;
        break;
    case Family::Debian:
        arguments = QStringList { QStringLiteral("-s"), QStringLiteral("remove") } + packages;
        break;
    case Family::Arch:
        arguments = QStringList { QStringLiteral("-Rcp"), QStringLiteral("--print-format"),
                                  QStringLiteral("%n") } + packages;
        break;
    case Family::Other:
        return {};
    }

    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(env);
    process.start(manager, arguments);
    if (!process.waitForFinished(60000)) return {};
    const QStringList lines = QString::fromLocal8Bit(process.readAllStandardOutput())
                                  .split(u'\n', Qt::SkipEmptyParts);

    QStringList names;
    bool inDependents = false;
    for (const QString &line : lines) {
        switch (family()) {
        case Family::Fedora:
            // " name  arch  version  repo  size" under "Removing dependent packages:".
            if (!line.startsWith(u' ')) {
                inDependents = line.startsWith(QLatin1String("Removing dependent packages"));
                continue;
            }
            if (inDependents) names << line.trimmed().section(u' ', 0, 0);
            break;
        case Family::Debian:
            // "Remv name [version]"
            if (line.startsWith(QLatin1String("Remv ")))
                names << line.section(u' ', 1, 1).section(u':', 0, 0);
            break;
        case Family::Arch:
            names << line.trimmed();
            break;
        case Family::Other:
            break;
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
    run();
}

void SystemPackages::remove(const QStringList &packages, bool withDependents)
{
    if (isBusy()) return;
    m_install        = false;
    m_withDependents = withDependents;
    m_names          = packages;
    run();
}

void SystemPackages::run()
{
    // The package manager runs as root behind the desktop's password prompt,
    // every time, whatever the system's rules for PackageKit would allow.
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    const QString manager = tool(family());
    if (pkexec.isEmpty() || manager.isEmpty() || m_names.isEmpty()) {
        done(false, tr("Packages are installed by the system's package manager."));
        return;
    }

    QStringList arguments;
    switch (family()) {
    case Family::Fedora:
        arguments = { manager, QStringLiteral("-y"),
                      m_install ? QStringLiteral("install") : QStringLiteral("remove") };
        arguments << m_names;
        break;
    case Family::Debian: {
        // apt-get does not refresh its lists by itself, and asks nothing with
        // a noninteractive front end; one shell keeps it to one prompt.
        const QString names = m_names.join(u' ');
        arguments = { QStringLiteral("/bin/sh"), QStringLiteral("-c"),
                      m_install
                          ? QStringLiteral("DEBIAN_FRONTEND=noninteractive %1 update -q && "
                                           "DEBIAN_FRONTEND=noninteractive %1 -y install %2")
                                .arg(manager, names)
                          : QStringLiteral("DEBIAN_FRONTEND=noninteractive %1 -y remove %2")
                                .arg(manager, names) };
        break;
    }
    case Family::Arch:
        arguments = { manager, QStringLiteral("--noconfirm"),
                      m_install ? QStringLiteral("-S")
                                : m_withDependents ? QStringLiteral("-Rc") : QStringLiteral("-R") };
        arguments << m_names;
        break;
    case Family::Other:
        break;
    }

    auto *process = new QProcess(this);
    m_running = true;
    connect(process, &QProcess::finished, this,
            [this, process](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        m_running = false;
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

void SystemPackages::done(bool ok, const QString &error)
{
    Q_EMIT finished(ok, error);
}
