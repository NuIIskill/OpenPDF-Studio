#include "app/UpdateChecker.hpp"

#include "app/AppSettings.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLatin1StringView>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>

namespace {

// The same list the tags page shows, as JSON. per_page is capped at 100 by
// GitHub; the newest tags come first, so one page is always enough.
constexpr auto kApiUrl =
    "https://api.github.com/repos/NuIIskill/OpenPDF-Studio/tags?per_page=100";
// Where the ready-made packages are - the tags above only answer "which
// version is the current one", not "where do I get it".
constexpr auto kDownloadPage = "https://openpdf-studio.nullskill.de/download.html";

// A tag broken into what can be compared: the numeric components, and
// whatever was appended after a '-' or '+' ("rc1", "beta.2", a build id).
struct Version {
    QList<int> parts;
    QString    pre;
    bool       valid { false };
};

Version parseVersion(const QString &tag)
{
    Version v;
    QString s = tag.trimmed();
    if (s.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        s.remove(0, 1);

    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s.at(i) == QLatin1Char('-') || s.at(i) == QLatin1Char('+')) {
            v.pre = s.mid(i + 1);
            s.truncate(i);
            break;
        }
    }

    const QStringList fields = s.split(QLatin1Char('.'));
    for (const QString &f : fields) {
        bool ok = false;
        const int n = f.toInt(&ok);
        if (!ok || n < 0)
            return v;          // not a version — the caller skips it
        v.parts.append(n);
    }
    v.valid = !v.parts.isEmpty();
    return v;
}

int compare(const Version &a, const Version &b)
{
    const qsizetype n = std::max(a.parts.size(), b.parts.size());
    for (qsizetype i = 0; i < n; ++i) {
        const int x = i < a.parts.size() ? a.parts.at(i) : 0;
        const int y = i < b.parts.size() ? b.parts.at(i) : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    // Same numbers: the finished release outranks its own pre-releases.
    if (a.pre == b.pre)      return 0;
    if (a.pre.isEmpty())     return 1;
    if (b.pre.isEmpty())     return -1;
    return a.pre < b.pre ? -1 : 1;
}

} // namespace

UpdateChecker::UpdateChecker(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

QUrl UpdateChecker::downloadPageUrl()
{
    return QUrl(QLatin1StringView(kDownloadPage));
}

QString UpdateChecker::currentVersion()
{
    return QString::fromLatin1(APP_VERSION);
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    const Version va = parseVersion(a);
    const Version vb = parseVersion(b);
    if (!va.valid || !vb.valid)
        return 0;
    return compare(va, vb);
}

QString UpdateChecker::newestVersion(const QStringList &tags)
{
    QString best;
    Version bestV;
    for (const QString &tag : tags) {
        const Version v = parseVersion(tag);
        if (!v.valid)
            continue;
        if (!bestV.valid || compare(v, bestV) > 0) {
            bestV = v;
            best  = tag.trimmed();
            if (best.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
                best.remove(0, 1);
        }
    }
    return best;
}

bool UpdateChecker::isDue(const QString &interval, const QDateTime &last,
                          const QDateTime &now)
{
    if (interval == QLatin1String("startup"))
        return true;
    // Never checked, or the clock has moved backwards since — either way
    // waiting for a date that may never come is worse than one more request.
    if (!last.isValid() || last > now)
        return true;

    const qint64 days = last.daysTo(now);
    if (interval == QLatin1String("weekly"))
        return days >= 7;
    if (interval == QLatin1String("monthly"))
        return days >= 30;
    return days >= 1;   // "daily", and anything unrecognised
}

void UpdateChecker::checkNow()
{
    if (m_reply)
        return;
    send();
}

bool UpdateChecker::checkIfDue()
{
    if (m_reply || !m_settings || !m_settings->autoUpdateCheck())
        return false;
    if (!isDue(m_settings->updateInterval(), m_settings->lastUpdateCheck(),
               QDateTime::currentDateTimeUtc()))
        return false;
    send();
    return true;
}

void UpdateChecker::send()
{
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);

    QNetworkRequest req{QUrl(QLatin1StringView(kApiUrl))};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    // GitHub rejects requests without one.
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("OpenPDF-Studio/%1").arg(currentVersion()));
    // A stalled connection must not leave the checker busy for the rest of
    // the session — the button in Advanced would stay dead.
    req.setTransferTimeout(15000);

    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::handleReply);
}

void UpdateChecker::handleReply()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();

    UpdateCheckResult r;
    r.current = currentVersion();

    if (reply->error() != QNetworkReply::NoError) {
        r.error = reply->errorString();
        Q_EMIT finished(r);
        return;
    }

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isArray()) {
        r.error = tr("Unexpected answer from GitHub.");
        Q_EMIT finished(r);
        return;
    }

    const QJsonArray arr = doc.array();
    QStringList tags;
    tags.reserve(arr.size());
    for (const QJsonValue &v : arr)
        tags.append(v.toObject().value(QLatin1String("name")).toString());

    r.latest = newestVersion(tags);
    if (r.latest.isEmpty()) {
        r.error = tr("No released version found.");
        Q_EMIT finished(r);
        return;
    }

    r.ok = true;
    r.updateAvailable = compareVersions(r.latest, r.current) > 0;
    // Only a check that got an answer counts against the interval; an
    // offline start should try again next time rather than wait a day.
    if (m_settings)
        m_settings->setLastUpdateCheck(QDateTime::currentDateTimeUtc());

    Q_EMIT finished(r);
}
