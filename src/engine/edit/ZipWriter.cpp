#include "engine/edit/ZipWriter.hpp"

namespace {

static uint32_t crc32Compute(const QByteArray &data)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : data) {
        crc ^= c;
        for (int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}

static void u16le(QByteArray &b, uint16_t v)
{
    b += char(v & 0xFF);
    b += char((v >> 8) & 0xFF);
}
static void u32le(QByteArray &b, uint32_t v)
{
    b += char(v & 0xFF);
    b += char((v >> 8)  & 0xFF);
    b += char((v >> 16) & 0xFF);
    b += char((v >> 24) & 0xFF);
}

}   // namespace

QByteArray ZipWriter::archive() const
{
    QList<Entry> entries = m_entries;

    QByteArray zip;
    for (Entry &e : entries)
        writeLocal(zip, e);

    QByteArray cd;
    for (const Entry &e : entries)
        writeCentral(cd, e);

    const quint32 cdOff  = static_cast<quint32>(zip.size());
    const quint32 cdSize = static_cast<quint32>(cd.size());
    zip += cd;

    zip += "\x50\x4B\x05\x06";
    u16le(zip, 0); u16le(zip, 0);
    u16le(zip, static_cast<uint16_t>(entries.size()));
    u16le(zip, static_cast<uint16_t>(entries.size()));
    u32le(zip, cdSize);
    u32le(zip, cdOff);
    u16le(zip, 0);
    return zip;
}

// qCompress emits a zlib stream prefixed with the uncompressed size:
//   [4 bytes size][2 bytes zlib header][deflate data][4 bytes adler32]
// ZIP method 8 wants the bare deflate data, so strip the 6-byte head and the
// 4-byte tail. Falls back to storing whenever that would not be a win.
void ZipWriter::deflateEntry(ZipWriter::Entry &e)
{
    e.method = 0;
    e.stored = e.data;
    if (e.data.size() < 256) return;
    const QByteArray z = qCompress(e.data, 9);
    if (z.size() <= 10) return;
    const QByteArray raw = z.mid(6, z.size() - 10);
    if (raw.isEmpty() || raw.size() >= e.data.size()) return;
    e.method = 8;
    e.stored = raw;
}

void ZipWriter::writeLocal(QByteArray &zip, ZipWriter::Entry &e)
{
    e.crc    = crc32Compute(e.data);
    deflateEntry(e);
    e.offset = static_cast<uint32_t>(zip.size());
    zip += "\x50\x4B\x03\x04";
    u16le(zip, 20); u16le(zip, 0); u16le(zip, e.method);
    u16le(zip, 0);  u16le(zip, 0);
    u32le(zip, e.crc);
    u32le(zip, static_cast<uint32_t>(e.stored.size()));
    u32le(zip, static_cast<uint32_t>(e.data.size()));
    u16le(zip, static_cast<uint16_t>(e.name.size()));
    u16le(zip, 0);
    zip += e.name;
    zip += e.stored;
}

void ZipWriter::writeCentral(QByteArray &cd, const ZipWriter::Entry &e)
{
    cd += "\x50\x4B\x01\x02";
    u16le(cd, 20); u16le(cd, 20); u16le(cd, 0); u16le(cd, e.method);
    u16le(cd, 0);  u16le(cd, 0);
    u32le(cd, e.crc);
    u32le(cd, static_cast<uint32_t>(e.stored.size()));
    u32le(cd, static_cast<uint32_t>(e.data.size()));
    u16le(cd, static_cast<uint16_t>(e.name.size()));
    u16le(cd, 0); u16le(cd, 0); u16le(cd, 0); u16le(cd, 0);
    u32le(cd, 0); u32le(cd, e.offset);
    cd += e.name;
}
