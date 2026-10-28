#include "app/FileDownload.hpp"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

FileDownload::FileDownload(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void FileDownload::start(const QUrl &url, const QByteArray &sha256Hex, const QString &target)
{
    if (isBusy()) return;
    QDir().mkpath(QFileInfo(target).absolutePath());
    m_file = new QSaveFile(target, this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        const QString error = m_file->errorString();
        delete m_file;
        m_file = nullptr;
        Q_EMIT finished(false, error);
        return;
    }
    m_expected = sha256Hex.toLower();
    m_hash.reset();

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OpenPDF Studio"));
    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        const QByteArray chunk = m_reply->readAll();
        m_hash.addData(chunk);
        m_file->write(chunk);
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total > 0) Q_EMIT progress(int(got * 100 / total));
    });
    connect(m_reply, &QNetworkReply::finished, this, &FileDownload::finish);
}

void FileDownload::finish()
{
    QNetworkReply *reply = m_reply;
    QSaveFile *file = m_file;
    m_reply = nullptr;
    m_file  = nullptr;
    reply->deleteLater();
    file->deleteLater();

    QString error;
    if (reply->error() != QNetworkReply::NoError) {
        error = reply->errorString();
    } else {
        const QByteArray rest = reply->readAll();
        m_hash.addData(rest);
        file->write(rest);
        // A file that is not exactly the expected one is never kept.
        if (m_hash.result().toHex() != m_expected)
            error = tr("The downloaded file is not the expected one.");
    }
    if (!error.isEmpty()) {
        file->cancelWriting();
        Q_EMIT finished(false, error);
        return;
    }
    if (!file->commit()) {
        Q_EMIT finished(false, file->errorString());
        return;
    }
    Q_EMIT finished(true, {});
}
