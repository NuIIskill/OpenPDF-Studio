#pragma once

#include <QByteArray>
#include <QStringList>

/// Self-made certificates kept as PEM files beside config.ini, for platforms without a writable store.
namespace FileCertificates {

bool        isFileId(const QString &id);
QStringList ids();
QByteArray  certificatePem(const QString &id);
QByteArray  keyPem(const QString &id);
QString     store(const QByteArray &certificatePem, const QByteArray &keyPem);
bool        remove(const QString &id);

}
