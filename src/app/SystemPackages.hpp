#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

/// Installs and removes packages of the Linux distribution through PackageKit.
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

    static bool available();

    static bool canChange();

    static QStringList dependents(const QStringList &packages);

    explicit SystemPackages(QObject *parent = nullptr);

    void install(const QStringList &packages);
    void remove(const QStringList &packages, bool withDependents);

    [[nodiscard]] bool isBusy() const { return !m_transaction.isEmpty() || m_starting; }

Q_SIGNALS:
    void finished(bool ok, const QString &error);

private Q_SLOTS:
    void onPackage(uint info, const QString &id, const QString &summary);
    void onErrorCode(uint code, const QString &details);
    void onFinished(uint exit, uint runtime);

private:
    enum class Step { Resolve, Change };

    void start();
    void begin(Step step);
    void run();
    void elevate();
    void listen(bool on);
    void done(bool ok, const QString &error);

    bool        m_install  { true };
    bool        m_withDependents { false };
    bool        m_starting { false };
    Step        m_step     { Step::Resolve };
    QStringList m_names;
    QString     m_transaction;
    QString     m_error;

    struct Found { QString id; int rank { 0 }; bool installed { false }; };
    QHash<QString, Found> m_found;
};
