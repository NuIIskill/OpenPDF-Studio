#include "app/PdfPwStore.hpp"

#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace {

QString key(const QString &filePath)
{
    if (filePath.isEmpty()) return {};
    const QString canonical = QFileInfo(filePath).canonicalFilePath();
    return canonical.isEmpty() ? QFileInfo(filePath).absoluteFilePath() : canonical;
}

QMutex                  &mutex() { static QMutex m; return m; }
QHash<QString, QString> &table() { static QHash<QString, QString> t; return t; }

}

namespace PdfPwStore {

void set(const QString &filePath, const QString &pw)
{
    const QString k = key(filePath);
    if (k.isEmpty()) return;
    QMutexLocker lock(&mutex());
    if (pw.isEmpty()) table().remove(k);
    else              table().insert(k, pw);
}

QString get(const QString &filePath)
{
    const QString k = key(filePath);
    if (k.isEmpty()) return {};
    QMutexLocker lock(&mutex());
    return table().value(k);
}

bool has(const QString &filePath)
{
    return !get(filePath).isEmpty();
}

void forget(const QString &filePath)
{
    const QString k = key(filePath);
    if (k.isEmpty()) return;
    QMutexLocker lock(&mutex());
    table().remove(k);
}

void clear()
{
    QMutexLocker lock(&mutex());
    table().clear();
}

std::string forQpdf(const QString &filePath)
{
    return get(filePath).toStdString();
}

}
