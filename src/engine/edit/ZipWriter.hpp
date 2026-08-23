#pragma once

#include <QByteArray>
#include <QList>

// Minimal ZIP writer. Deflates an entry when that comes out smaller and stores
// it otherwise — DOCX, ODT and friends are ZIP containers, so an exporter only
// needs this much of the format.
class ZipWriter
{
public:
    void add(const QByteArray &name, const QByteArray &data)
    { m_entries.append({ name, data, {}, 0, 0, 0 }); }

    // The finished archive.
    QByteArray archive() const;

private:
    struct Entry {
        QByteArray name;
        QByteArray data;
        QByteArray stored;   // data as written: deflated or a copy
        quint32    crc    { 0 };
        quint32    offset { 0 };
        quint16    method { 0 };   // 0 = stored, 8 = deflate
    };
    static void deflateEntry(Entry &e);
    static void writeLocal(QByteArray &zip, Entry &e);
    static void writeCentral(QByteArray &cd, const Entry &e);

    QList<Entry> m_entries;
};
