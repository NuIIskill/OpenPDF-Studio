#include "engine/import/ZipArchive.hpp"

#include <QFile>

#include <cstring>

#ifdef HAVE_ZLIB
#  include <zlib.h>
#endif

namespace {

// A docx or odt is read whole into memory. These caps keep a crafted or broken
// file from taking the process down with it.
constexpr qint64 MaxArchiveBytes = 256LL * 1024 * 1024;
constexpr qint64 MaxEntryBytes   = 128LL * 1024 * 1024;
constexpr int    MaxEntries      = 20000;

constexpr int EndOfCentralDirSize = 22;
constexpr int CentralHeaderSize   = 46;
constexpr int LocalHeaderSize     = 30;

quint16 u16(const QByteArray &b, qint64 at)
{
    return static_cast<quint16>(static_cast<quint8>(b[at]))
         | static_cast<quint16>(static_cast<quint8>(b[at + 1])) << 8;
}

quint32 u32(const QByteArray &b, qint64 at)
{
    return static_cast<quint32>(static_cast<quint8>(b[at]))
         | static_cast<quint32>(static_cast<quint8>(b[at + 1])) << 8
         | static_cast<quint32>(static_cast<quint8>(b[at + 2])) << 16
         | static_cast<quint32>(static_cast<quint8>(b[at + 3])) << 24;
}

quint64 u64(const QByteArray &b, qint64 at)
{
    return static_cast<quint64>(u32(b, at))
         | static_cast<quint64>(u32(b, at + 4)) << 32;
}

bool signatureAt(const QByteArray &b, qint64 at, const char *sig)
{
    if (at < 0 || at + 4 > b.size()) return false;
    return std::memcmp(b.constData() + at, sig, 4) == 0;
}

/// Sizes and the local header offset move into the zip64 extra field once any
/// of them no longer fits in 32 bits, marked by 0xFFFFFFFF in the fixed fields.
void applyZip64Extra(const QByteArray &extra, qint64 &uncompressed,
                     qint64 &compressed, qint64 &offset)
{
    qint64 at = 0;
    while (at + 4 <= extra.size()) {
        const quint16 id   = u16(extra, at);
        const quint16 size = u16(extra, at + 2);
        at += 4;
        if (at + size > extra.size()) return;
        if (id == 0x0001) {
            qint64 field = at;
            if (uncompressed == 0xFFFFFFFF && field + 8 <= at + size) {
                uncompressed = static_cast<qint64>(u64(extra, field));
                field += 8;
            }
            if (compressed == 0xFFFFFFFF && field + 8 <= at + size) {
                compressed = static_cast<qint64>(u64(extra, field));
                field += 8;
            }
            if (offset == 0xFFFFFFFF && field + 8 <= at + size)
                offset = static_cast<qint64>(u64(extra, field));
            return;
        }
        at += size;
    }
}

QByteArray inflateRaw(const QByteArray &in, qint64 expected, QString *error)
{
#ifndef HAVE_ZLIB
    Q_UNUSED(in)
    Q_UNUSED(expected)
    if (error) *error = QStringLiteral("built without zlib");
    return {};
#else
    QByteArray out;
    out.resize(static_cast<int>(expected));

    z_stream stream {};
    // A negative window size selects a raw deflate stream: inside a ZIP there is
    // no zlib header and no adler32 trailer.
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        if (error) *error = QStringLiteral("inflateInit2 failed");
        return {};
    }

    stream.next_in   = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
    stream.avail_in  = static_cast<uInt>(in.size());
    stream.next_out  = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());

    const int rc = inflate(&stream, Z_FINISH);
    const qint64 written = static_cast<qint64>(stream.total_out);
    inflateEnd(&stream);

    if (rc != Z_STREAM_END || written != expected) {
        if (error) *error = QStringLiteral("inflate failed (%1, %2 of %3 bytes)")
                                .arg(rc).arg(written).arg(expected);
        return {};
    }
    return out;
#endif
}

}

bool ZipArchive::available()
{
#ifdef HAVE_ZLIB
    return true;
#else
    return false;
#endif
}

bool ZipArchive::open(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (file.size() > MaxArchiveBytes) {
        if (error) *error = QStringLiteral("file larger than %1 bytes").arg(MaxArchiveBytes);
        return false;
    }
    return open(file.readAll(), error);
}

bool ZipArchive::open(const QByteArray &data, QString *error)
{
    m_entries.clear();
    m_data = data;

    if (m_data.size() < EndOfCentralDirSize) {
        if (error) *error = QStringLiteral("not a zip container");
        return false;
    }
    if (!readCentralDirectory(error)) {
        m_data.clear();
        m_entries.clear();
        return false;
    }
    return true;
}

bool ZipArchive::readCentralDirectory(QString *error)
{
    // The end record sits last but may be followed by up to 64k of comment.
    const qint64 lowest = qMax<qint64>(0, m_data.size() - EndOfCentralDirSize - 0xFFFF);
    qint64 end = -1;
    for (qint64 at = m_data.size() - EndOfCentralDirSize; at >= lowest; --at) {
        if (signatureAt(m_data, at, "PK\x05\x06")) { end = at; break; }
    }
    if (end < 0) {
        if (error) *error = QStringLiteral("no end of central directory record");
        return false;
    }

    qint64 count  = u16(m_data, end + 10);
    qint64 offset = u32(m_data, end + 16);

    // zip64: the real values live in a separate record the locator points at.
    const qint64 locator = end - 20;
    if (signatureAt(m_data, locator, "PK\x06\x07")) {
        const qint64 record = static_cast<qint64>(u64(m_data, locator + 8));
        if (signatureAt(m_data, record, "PK\x06\x06") && record + 56 <= m_data.size()) {
            count  = static_cast<qint64>(u64(m_data, record + 32));
            offset = static_cast<qint64>(u64(m_data, record + 48));
        }
    }

    if (count < 0 || count > MaxEntries) {
        if (error) *error = QStringLiteral("%1 entries").arg(count);
        return false;
    }

    qint64 at = offset;
    for (qint64 i = 0; i < count; ++i) {
        if (!signatureAt(m_data, at, "PK\x01\x02")
                || at + CentralHeaderSize > m_data.size()) {
            if (error) *error = QStringLiteral("central directory ends after %1 entries").arg(i);
            return false;
        }

        Entry entry;
        entry.flags         = u16(m_data, at + 8);
        entry.method        = u16(m_data, at + 10);
        entry.compressed    = u32(m_data, at + 20);
        entry.uncompressed  = u32(m_data, at + 24);
        entry.offset        = u32(m_data, at + 42);

        const qint64 nameLength    = u16(m_data, at + 28);
        const qint64 extraLength   = u16(m_data, at + 30);
        const qint64 commentLength = u16(m_data, at + 32);
        const qint64 next = at + CentralHeaderSize + nameLength + extraLength + commentLength;
        if (next > m_data.size()) {
            if (error) *error = QStringLiteral("central directory entry out of bounds");
            return false;
        }

        const QByteArray name  = m_data.mid(at + CentralHeaderSize, nameLength);
        const QByteArray extra = m_data.mid(at + CentralHeaderSize + nameLength, extraLength);
        applyZip64Extra(extra, entry.uncompressed, entry.compressed, entry.offset);

        // Bit 11 says the name is UTF-8; everything else is read as latin1 here,
        // which is right for the ASCII part names a docx or odt is made of.
        const QString key = (entry.flags & 0x0800)
            ? QString::fromUtf8(name) : QString::fromLatin1(name);
        if (!key.isEmpty() && !key.endsWith(QLatin1Char('/')))
            m_entries.insert(key, entry);

        at = next;
    }
    return true;
}

QByteArray ZipArchive::read(const QString &name, QString *error) const
{
    const auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd()) {
        if (error) *error = QStringLiteral("no entry \"%1\"").arg(name);
        return {};
    }
    const Entry &entry = *it;

    if (entry.flags & 0x0001) {
        if (error) *error = QStringLiteral("\"%1\" is encrypted").arg(name);
        return {};
    }
    if (entry.uncompressed > MaxEntryBytes || entry.compressed > MaxEntryBytes) {
        if (error) *error = QStringLiteral("\"%1\" is too large").arg(name);
        return {};
    }
    if (!signatureAt(m_data, entry.offset, "PK\x03\x04")
            || entry.offset + LocalHeaderSize > m_data.size()) {
        if (error) *error = QStringLiteral("\"%1\" has no local header").arg(name);
        return {};
    }

    // The local header repeats the sizes but may leave them zero and put them in
    // a data descriptor behind the data, so only its two length fields are used.
    const qint64 nameLength  = u16(m_data, entry.offset + 26);
    const qint64 extraLength = u16(m_data, entry.offset + 28);
    const qint64 start = entry.offset + LocalHeaderSize + nameLength + extraLength;
    if (start + entry.compressed > m_data.size()) {
        if (error) *error = QStringLiteral("\"%1\" runs past the end of the file").arg(name);
        return {};
    }

    const QByteArray raw = m_data.mid(start, entry.compressed);
    if (entry.method == 0) {
        if (raw.size() != entry.uncompressed) {
            if (error) *error = QStringLiteral("\"%1\" has a wrong stored size").arg(name);
            return {};
        }
        return raw;
    }
    if (entry.method != 8) {
        if (error) *error = QStringLiteral("\"%1\" uses compression method %2")
                                .arg(name).arg(entry.method);
        return {};
    }

    QString reason;
    const QByteArray out = inflateRaw(raw, entry.uncompressed, &reason);
    if (out.isEmpty() && entry.uncompressed > 0) {
        if (error) *error = QStringLiteral("\"%1\": %2").arg(name, reason);
        return {};
    }
    return out;
}
