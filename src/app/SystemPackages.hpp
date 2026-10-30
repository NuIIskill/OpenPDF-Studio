#pragma once

#include <QObject>
#include <QStringList>

/// Installs and removes packages of the Linux distribution, behind the system's password prompt.
class SystemPackages : public QObject
{
    Q_OBJECT

public:
    enum class Family { Fedora, Debian, Arch, Other };

    struct Names {
        QStringList fedora;
        QStringList debian;
        QStringList arch;
    };

    static Family family();

    static QStringList namesFor(const Names &names);

    static QString command(const QStringList &packages, bool install);

    static bool canChange();

    static QStringList dependents(const QStringList &packages);

    explicit SystemPackages(QObject *parent = nullptr);

    void install(const QStringList &packages);
    void remove(const QStringList &packages, bool withDependents);

    [[nodiscard]] bool isBusy() const { return m_running; }

Q_SIGNALS:
    void finished(bool ok, const QString &error);

private:
    void run();
    void done(bool ok, const QString &error);

    bool        m_install        { true };
    bool        m_withDependents { false };
    bool        m_running        { false };
    QStringList m_names;
};
