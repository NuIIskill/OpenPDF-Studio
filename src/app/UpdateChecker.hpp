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

// What one check found out.
struct UpdateCheckResult {
    bool    ok              { false };  // the query itself went through
    bool    updateAvailable { false };  // ok && latest > current
    QString latest;                     // newest tag, without the "v"
    QString current;                    // APP_VERSION
    QString error;                      // only set when !ok
};

// Asks GitHub for the repository's tags and compares the newest one against
// the running version. Nothing is downloaded or installed — a found update is
// a sentence and a link to the download page, which is all the promise the
// setting in Advanced ever made.
//
// Two different addresses, on purpose: the tags say which version is current,
// the download page is where a user gets it. The tags are read through the API
// rather than by scraping <https://github.com/NuIIskill/OpenPDF-Studio/tags>:
// same list, but as JSON that will not change shape the next time GitHub
// redesigns that page.
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(AppSettings *settings, QObject *parent = nullptr);

    // The page a user is sent to. Not GitHub: the packages live here.
    [[nodiscard]] static QUrl downloadPageUrl();
    // The running version, as written in version.txt at build time.
    [[nodiscard]] static QString currentVersion();

    // Runs a check regardless of the setting. For the button in Advanced,
    // where the click *is* the request.
    void checkNow();
    // Runs one only if automatic checks are on and the interval has passed.
    // Returns whether a request went out.
    bool checkIfDue();
    [[nodiscard]] bool isBusy() const { return m_reply != nullptr; }

    // ── The pure parts, so they can be reasoned about on their own ─────────

    /// -1 / 0 / 1 for a < b, a == b, a > b. Leading "v" is ignored, missing
    /// components count as zero ("1.2" == "1.2.0"), and a suffix marks a
    /// pre-release: "1.2.0-rc1" < "1.2.0". Unparsable input compares as 0.
    [[nodiscard]] static int compareVersions(const QString &a, const QString &b);
    /// The highest of `tags`, ignoring the ones that are not versions.
    /// Empty when none of them is.
    [[nodiscard]] static QString newestVersion(const QStringList &tags);
    /// Whether a check is owed, given when the last one succeeded.
    /// An invalid `last` (never checked) is always due.
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
