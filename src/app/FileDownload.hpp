#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QObject>
#include <QString>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;
QT_END_NAMESPACE

/// Downloads one file and keeps it only when its SHA-256 is the expected one.
class FileDownload : public QObject
{
    Q_OBJECT

public:
    explicit FileDownload(QObject *parent = nullptr);

    void start(const QUrl &url, const QByteArray &sha256Hex, const QString &target);

    [[nodiscard]] bool isBusy() const { return m_reply != nullptr; }

Q_SIGNALS:
    void progress(int percent);
    void finished(bool ok, const QString &error);

private:
    void finish();

    QNetworkAccessManager *m_nam   { nullptr };
    QNetworkReply         *m_reply { nullptr };
    QSaveFile             *m_file  { nullptr };
    QCryptographicHash     m_hash  { QCryptographicHash::Sha256 };
    QByteArray             m_expected;
};
