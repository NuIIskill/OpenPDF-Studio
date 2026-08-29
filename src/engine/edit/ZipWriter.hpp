#pragma once

#include <QByteArray>
#include <QList>

/// Minimal ZIP writer.
class ZipWriter
{
public:
    void add(const QByteArray &name, const QByteArray &data)
    { m_entries.append({ name, data, {}, 0, 0, 0 }); }

    QByteArray archive() const;

private:
    struct Entry {
        QByteArray name;
        QByteArray data;
        QByteArray stored;
        quint32    crc    { 0 };
        quint32    offset { 0 };
        quint16    method { 0 };
    };
    static void deflateEntry(Entry &e);
    static void writeLocal(QByteArray &zip, Entry &e);
    static void writeCentral(QByteArray &cd, const Entry &e);

    QList<Entry> m_entries;
};
