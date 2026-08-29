#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QDateTime;
class QNetworkAccessManager;
class QNetworkReply;
QT_END_NAMESPACE

class AppSettings;

/// Stores the result of an update check.
struct UpdateCheckResult {
    bool    ok              { false };
    bool    updateAvailable { false };
    QString latest;
    QString current;
    QString error;
};

/// Checks available releases against the running version.
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(AppSettings *settings, QObject *parent = nullptr);

    [[nodiscard]] static QUrl downloadPageUrl();

    [[nodiscard]] static QString currentVersion();

    void checkNow();

    bool checkIfDue();
    [[nodiscard]] bool isBusy() const { return m_reply != nullptr; }

    [[nodiscard]] static int compareVersions(const QString &a, const QString &b);

    [[nodiscard]] static QString newestVersion(const QStringList &tags);

    [[nodiscard]] static bool isDue(const QString &interval,
                                    const QDateTime &last,
                                    const QDateTime &now);

Q_SIGNALS:
    void finished(const UpdateCheckResult &result);

private:
    void send();
    void handleReply();

    AppSettings           *m_settings { nullptr };
    QNetworkAccessManager *m_nam      { nullptr };
    QNetworkReply         *m_reply    { nullptr };
};
