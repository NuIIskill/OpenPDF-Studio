#include "engine/export/FontProgram.hpp"

#include <cmath>
#include <limits>

namespace {

quint16 u16(const QByteArray &d, qsizetype at)
{
    return at >= 0 && at + 2 <= d.size() ? quint16((quint8(d.at(at)) << 8) | quint8(d.at(at + 1))) : 0;
}

quint32 u32(const QByteArray &d, qsizetype at) { return (quint32(u16(d, at)) << 16) | u16(d, at + 2); }

QByteArray sfntTable(const QByteArray &font, const char *tag)
{
    const int count = u16(font, 4);
    for (int i = 0; i < count; ++i) {
        const qsizetype at = 12 + 16 * qsizetype(i);
        if (font.mid(at, 4) != tag) continue;
        const quint32 offset = u32(font, at + 8), length = u32(font, at + 12);
        if (quint64(offset) + length > quint64(font.size())) return {};
        return font.mid(offset, length);
    }
    return {};
}

qsizetype subtable(const QByteArray &cmap, int platform, int encoding)
{
    for (int i = 0; i < u16(cmap, 2); ++i) {
        const qsizetype at = 4 + 8 * qsizetype(i);
        if (u16(cmap, at) == platform && u16(cmap, at + 2) == encoding) {
            const quint32 offset = u32(cmap, at + 4);
            return offset < quint32(cmap.size()) ? qsizetype(offset) : -1;
        }
    }
    return -1;
}

int lookup(const QByteArray &cmap, qsizetype t, quint32 code)
{
    switch (u16(cmap, t)) {
    case 0:
        return code < 256 && t + 6 + qsizetype(code) < cmap.size() ? quint8(cmap.at(t + 6 + qsizetype(code))) : -1;
    case 4: {
        const int segments = u16(cmap, t + 6) / 2;
        const qsizetype ends = t + 14, starts = ends + 2 * segments + 2;
        const qsizetype deltas = starts + 2 * segments, ranges = deltas + 2 * segments;
        for (int i = 0; i < segments; ++i) {
            if (code > u16(cmap, ends + 2 * i)) continue;
            const quint16 start = u16(cmap, starts + 2 * i);
            if (code < start) return -1;
            const quint16 delta = u16(cmap, deltas + 2 * i), range = u16(cmap, ranges + 2 * i);
            if (range == 0) return quint16(code + delta);
            const quint16 glyph = u16(cmap, ranges + 2 * i + range + 2 * qsizetype(code - start));
            return glyph ? quint16(glyph + delta) : 0;
        }
        return -1;
    }
    case 6: {
        const quint16 first = u16(cmap, t + 6), count = u16(cmap, t + 8);
        if (code < first || code >= quint32(first) + count) return -1;
        return u16(cmap, t + 10 + 2 * qsizetype(code - first));
    }
    case 12: {
        const quint32 groups = u32(cmap, t + 12);
        for (quint32 i = 0; i < groups && t + 16 + 12 * qsizetype(i) + 12 <= cmap.size(); ++i) {
            const qsizetype g = t + 16 + 12 * qsizetype(i);
            if (code >= u32(cmap, g) && code <= u32(cmap, g + 4))
                return int(u32(cmap, g + 8) + code - u32(cmap, g));
        }
        return -1;
    }
    default:
        return -1;
    }
}

}

bool FontProgram::hasCmap(int platform, int encoding) const
{
    return subtable(cmap, platform, encoding) >= 0;
}

int FontProgram::cmapCount() const
{
    return u16(cmap, 2);
}

int FontProgram::glyphFromCmap(int platform, int encoding, quint32 code) const
{
    const qsizetype t = subtable(cmap, platform, encoding);
    return t < 0 ? -1 : lookup(cmap, t, code);
}

double FontProgram::width(int glyph) const
{
    return glyph >= 0 && size_t(glyph) < widths.size() ? widths[size_t(glyph)]
                                                       : std::numeric_limits<double>::quiet_NaN();
}

FontProgram FontProgram::trueType(const QByteArray &data)
{
    FontProgram p;
    const QByteArray head = sfntTable(data, "head"), hhea = sfntTable(data, "hhea");
    const QByteArray hmtx = sfntTable(data, "hmtx"), maxp = sfntTable(data, "maxp");
    if (head.isEmpty() || maxp.isEmpty()) return p;
    p.kind = Kind::TrueType;
    p.glyphCount = u16(maxp, 4);
    p.cmap = sfntTable(data, "cmap");
    const double scale = 1000.0 / qMax(16, int(u16(head, 18)));
    const int metrics = u16(hhea, 34);
    p.widths.assign(size_t(p.glyphCount), std::numeric_limits<double>::quiet_NaN());
    for (int g = 0; g < p.glyphCount && metrics > 0; ++g) {
        const qsizetype at = 4 * qsizetype(qMin(g, metrics - 1));
        if (at + 2 <= hmtx.size()) p.widths[size_t(g)] = u16(hmtx, at) * scale;
    }
    // Names past the 258 standard Macintosh ones, which readers try when the cmap has no answer.
    const QByteArray post = sfntTable(data, "post");
    if (u32(post, 0) == 0x00020000) {
        const int count = u16(post, 32);
        std::vector<QByteArray> names;
        for (qsizetype at = 34 + 2 * qsizetype(count); at < post.size();) {
            const int length = quint8(post.at(at));
            names.push_back(post.mid(at + 1, length));
            at += 1 + length;
        }
        for (int g = 0; g < count; ++g) {
            const int index = u16(post, 34 + 2 * qsizetype(g));
            if (index >= 258 && size_t(index - 258) < names.size())
                p.glyphByName.insert(names[size_t(index - 258)], g);
        }
    }
    return p;
}

FontProgram FontProgram::openType(const QByteArray &data)
{
    const QByteArray cffTable = sfntTable(data, "CFF ");
    if (cffTable.isEmpty()) return trueType(data);
    FontProgram p = cff(cffTable);
    p.cmap = sfntTable(data, "cmap");
    return p;
}
