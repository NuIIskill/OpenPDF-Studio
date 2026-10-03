#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

QT_BEGIN_NAMESPACE
class QImage;
QT_END_NAMESPACE

/// Handwritten signatures the user kept as templates, stored as PNG files beside config.ini.
namespace SignatureStore {

/// One saved signature; name and creation time live in the PNG's text chunks.
struct Entry {
    QString   path;
    QString   name;
    QDateTime created;
};

QList<Entry> list();
QString      save(const QImage &image, const QString &name);
bool         rename(const QString &path, const QString &name);
bool         remove(const QString &path);

}
