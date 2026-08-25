// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/engine/MediaFormat.hpp"

#include <QFile>
#include <QFileInfo>

namespace {

// ── ISO base media file format (MP4, MOV, 3GP) ───────────────────────────────
//
// The file is a tree of boxes, each "size (4) + type (4)" followed by its
// payload. Everything needed here sits on one path: moov → trak → mdia → minf
// → stbl → stsd, whose first sample entry names the codec by four characters.
// mvhd gives the duration, tkhd the display size.

struct Box { QByteArray type; qint64 payloadStart { 0 }; qint64 payloadEnd { 0 }; };

quint32 beU32(const uchar *p) { return (quint32(p[0]) << 24) | (quint32(p[1]) << 16)
                                     | (quint32(p[2]) << 8)  |  quint32(p[3]); }
quint64 beU64(const uchar *p) { return (quint64(beU32(p)) << 32) | beU32(p + 4); }

/// Reads the box header at `at`. Invalid type means "stop here".
Box readBox(QFile &file, qint64 at, qint64 limit)
{
    Box box;
    if (at + 8 > limit || !file.seek(at)) return box;
    QByteArray header = file.read(8);
    if (header.size() != 8) return box;

    const auto *raw = reinterpret_cast<const uchar *>(header.constData());
    qint64 size = beU32(raw);
    box.type = header.mid(4, 4);
    qint64 headerSize = 8;

    if (size == 1) {                    // 64-bit size follows the type
        QByteArray large = file.read(8);
        if (large.size() != 8) return {};
        size = qint64(beU64(reinterpret_cast<const uchar *>(large.constData())));
        headerSize = 16;
    } else if (size == 0) {             // extends to the end of the file
        size = limit - at;
    }
    if (size < headerSize || at + size > limit) return {};

    box.payloadStart = at + headerSize;
    box.payloadEnd   = at + size;
    return box;
}

/// First direct child of `type` between `from` and `to`.
Box findBox(QFile &file, const char *type, qint64 from, qint64 to)
{
    qint64 at = from;
    while (at + 8 <= to) {
        const Box box = readBox(file, at, to);
        if (box.type.isEmpty()) break;
        if (box.type == type) return box;
        at = box.payloadEnd;
    }
    return {};
}

QString codecFromSampleEntry(const QByteArray &fourcc)
{
    static const struct { const char *tag, *name; } kCodecs[] = {
        { "avc1", "h264" },  { "avc3", "h264" },  { "h264", "h264" },
        { "hvc1", "hevc" },  { "hev1", "hevc" },
        { "av01", "av1" },   { "vp09", "vp9" },   { "vp08", "vp8" },
        { "mp4v", "mpeg4" }, { "s263", "h263" },  { "jpeg", "mjpeg" },
    };
    for (const auto &c : kCodecs)
        if (fourcc == c.tag) return QLatin1String(c.name);
    return QString::fromLatin1(fourcc).trimmed();
}

/// Walks one trak and reports its codec and display size when it is video.
bool readVideoTrack(QFile &file, const Box &trak, MediaFormat::Info *info)
{
    const Box mdia = findBox(file, "mdia", trak.payloadStart, trak.payloadEnd);
    if (mdia.type.isEmpty()) return false;
    const Box minf = findBox(file, "minf", mdia.payloadStart, mdia.payloadEnd);
    if (minf.type.isEmpty()) return false;
    // vmhd only exists in a video track's media information; its absence is
    // the cheapest way to skip audio and subtitle tracks.
    if (findBox(file, "vmhd", minf.payloadStart, minf.payloadEnd).type.isEmpty())
        return false;
    const Box stbl = findBox(file, "stbl", minf.payloadStart, minf.payloadEnd);
    if (stbl.type.isEmpty()) return false;
    const Box stsd = findBox(file, "stsd", stbl.payloadStart, stbl.payloadEnd);
    if (stsd.type.isEmpty()) return false;

    // stsd: version+flags (4), entry count (4), then the sample entries.
    if (!file.seek(stsd.payloadStart + 8)) return false;
    const QByteArray entry = file.read(16);
    if (entry.size() < 16) return false;
    info->videoCodec = codecFromSampleEntry(entry.mid(4, 4));

    // tkhd holds the display size as 16.16 fixed point in its last 8 bytes.
    const Box tkhd = findBox(file, "tkhd", trak.payloadStart, trak.payloadEnd);
    if (!tkhd.type.isEmpty() && file.seek(tkhd.payloadEnd - 8)) {
        const QByteArray wh = file.read(8);
        if (wh.size() == 8) {
            const auto *raw = reinterpret_cast<const uchar *>(wh.constData());
            const int w = int(beU32(raw)     >> 16);
            const int h = int(beU32(raw + 4) >> 16);
            if (w > 0 && h > 0) info->size = QSize(w, h);
        }
    }
    return true;
}

void readMp4(QFile &file, MediaFormat::Info *info)
{
    const qint64 end = file.size();
    info->container = QStringLiteral("mp4");
    info->readable  = true;

    const Box moov = findBox(file, "moov", 0, end);
    if (moov.type.isEmpty()) return;

    // mvhd: version (1) + flags (3), then times. Timescale and duration sit at
    // 12/16 for version 0 and 20/24 for version 1.
    const Box mvhd = findBox(file, "mvhd", moov.payloadStart, moov.payloadEnd);
    if (!mvhd.type.isEmpty() && file.seek(mvhd.payloadStart)) {
        const QByteArray head = file.read(32);
        if (head.size() >= 32) {
            const auto *raw = reinterpret_cast<const uchar *>(head.constData());
            const quint32 timescale = raw[0] == 1 ? beU32(raw + 20) : beU32(raw + 12);
            const quint64 duration  = raw[0] == 1 ? beU64(raw + 24) : beU32(raw + 16);
            if (timescale > 0) info->durationSec = double(duration) / timescale;
        }
    }

    qint64 at = moov.payloadStart;
    while (at + 8 <= moov.payloadEnd) {
        const Box box = readBox(file, at, moov.payloadEnd);
        if (box.type.isEmpty()) break;
        if (box.type == "trak" && readVideoTrack(file, box, info)) return;
        at = box.payloadEnd;
    }
}

// ── The other containers ─────────────────────────────────────────────────────
//
// Only identified, not walked: none of them is the answer we are looking for,
// so the codec inside does not change what happens next.

QString containerFromSignature(const QByteArray &head)
{
    if (head.size() >= 12 && head.mid(4, 4) == "ftyp") return QStringLiteral("mp4");
    if (head.startsWith(QByteArray::fromHex("1A45DFA3")))  return QStringLiteral("matroska");
    if (head.startsWith("RIFF") && head.mid(8, 4) == "AVI ") return QStringLiteral("avi");
    if (head.startsWith(QByteArray::fromHex("3026B2758E66CF11"))) return QStringLiteral("asf");
    if (head.startsWith("OggS")) return QStringLiteral("ogg");
    if (head.startsWith("FLV"))  return QStringLiteral("flv");
    if (head.startsWith(QByteArray::fromHex("000001BA"))
     || head.startsWith(QByteArray::fromHex("000001B3"))) return QStringLiteral("mpeg");
    return QString();
}

} // namespace

bool MediaFormat::Info::videoIsH264() const
{
    return videoCodec == QLatin1String("h264");
}

bool MediaFormat::Info::playsEverywhere() const
{
    return readable && container == QLatin1String("mp4") && videoIsH264();
}

MediaFormat::Info MediaFormat::inspect(const QString &path)
{
    Info info;
    if (path.isEmpty() || !QFileInfo::exists(path)) return info;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return info;

    const QByteArray head = file.read(16);
    const QString container = containerFromSignature(head);
    if (container.isEmpty()) return info;

    if (container == QLatin1String("mp4")) {
        readMp4(file, &info);
    } else {
        info.container = container;
        info.readable  = true;
    }
    return info;
}
