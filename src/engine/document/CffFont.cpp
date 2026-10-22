#include "engine/document/CffFont.hpp"

#include <QRawFont>
#include <QString>

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

void put16(QByteArray &out, quint32 v)
{
    out.append(char((v >> 8) & 0xFF)).append(char(v & 0xFF));
}

void put32(QByteArray &out, quint32 v)
{
    put16(out, v >> 16);
    put16(out, v & 0xFFFF);
}

struct Index { int count { 0 }; qsizetype dataStart { 0 }; QList<qsizetype> offsets; qsizetype end { 0 }; };

std::optional<Index> readIndex(const QByteArray &d, qsizetype at)
{
    const auto byte = [&](qsizetype i) { return i < d.size() ? int(quint8(d.at(i))) : -1; };
    if (byte(at) < 0 || byte(at + 1) < 0) return std::nullopt;
    Index index;
    index.count = (byte(at) << 8) | byte(at + 1);
    if (index.count == 0) { index.end = at + 2; return index; }
    const int offSize = byte(at + 2);
    if (offSize < 1 || offSize > 4) return std::nullopt;
    for (int i = 0; i <= index.count; ++i) {
        qsizetype v = 0;
        for (int b = 0; b < offSize; ++b) {
            const int c = byte(at + 3 + qsizetype(i) * offSize + b);
            if (c < 0) return std::nullopt;
            v = (v << 8) | c;
        }
        index.offsets.append(v);
    }
    index.dataStart = at + 3 + qsizetype(index.count + 1) * offSize - 1;
    index.end = index.dataStart + index.offsets.last();
    return index.end <= d.size() ? std::optional<Index>(index) : std::nullopt;
}

// The operators of a top dictionary with their operands.
QList<QPair<int, QList<double>>> readDict(const QByteArray &d, qsizetype from, qsizetype to)
{
    QList<QPair<int, QList<double>>> out;
    QList<double> operands;
    qsizetype i = from;
    const auto byte = [&](qsizetype k) { return k < to ? int(quint8(d.at(k))) : 0; };
    while (i < to) {
        const int b0 = byte(i);
        if (b0 <= 21) {
            int op = b0;
            if (b0 == 12) { op = 1200 + byte(i + 1); ++i; }
            out.append({ op, operands });
            operands.clear();
            ++i;
        } else if (b0 == 28) {
            operands.append(double(qint16((byte(i + 1) << 8) | byte(i + 2))));
            i += 3;
        } else if (b0 == 29) {
            operands.append(double(qint32((quint32(byte(i + 1)) << 24) | (byte(i + 2) << 16)
                                          | (byte(i + 3) << 8) | byte(i + 4))));
            i += 5;
        } else if (b0 == 30) {
            QString number;
            bool done = false;
            for (++i; i < to && !done; ++i) {
                for (const int nibble : { byte(i) >> 4, byte(i) & 0xF }) {
                    if (nibble <= 9) number += QChar(u'0' + nibble);
                    else if (nibble == 0xA) number += u'.';
                    else if (nibble == 0xB) number += u'E';
                    else if (nibble == 0xC) number += QStringLiteral("E-");
                    else if (nibble == 0xE) number += u'-';
                    else if (nibble == 0xF) { done = true; break; }
                }
            }
            operands.append(number.toDouble());
        } else if (b0 >= 32 && b0 <= 246) {
            operands.append(double(b0 - 139));
            ++i;
        } else if (b0 >= 247 && b0 <= 250) {
            operands.append(double((b0 - 247) * 256 + byte(i + 1) + 108));
            i += 2;
        } else if (b0 >= 251 && b0 <= 254) {
            operands.append(double(-(b0 - 251) * 256 - byte(i + 1) - 108));
            i += 2;
        } else {
            ++i;
        }
    }
    return out;
}

QByteArray cmapTable(const QList<QPair<quint32, quint32>> &map)
{
    // One format 4 subtable for Unicode, a segment for each run of codes whose glyphs run alike.
    struct Segment { quint32 start, end, delta; };
    QList<Segment> segments;
    for (const auto &[code, glyph] : map) {
        if (!segments.isEmpty() && segments.last().end + 1 == code
                && ((glyph - code) & 0xFFFF) == segments.last().delta) {
            segments.last().end = code;
            continue;
        }
        segments.append({ code, code, (glyph - code) & 0xFFFF });
    }
    segments.append({ 0xFFFF, 0xFFFF, 1 });
    const quint32 count = quint32(segments.size());
    quint32 range = 1, selector = 0;
    while (range * 2 <= count) { range *= 2; ++selector; }
    QByteArray sub;
    put16(sub, 4);
    put16(sub, 16 + 8 * count);
    put16(sub, 0);
    put16(sub, count * 2);
    put16(sub, range * 2);
    put16(sub, selector);
    put16(sub, count * 2 - range * 2);
    for (const Segment &s : std::as_const(segments)) put16(sub, s.end);
    put16(sub, 0);
    for (const Segment &s : std::as_const(segments)) put16(sub, s.start);
    for (const Segment &s : std::as_const(segments)) put16(sub, s.delta);
    for (int i = 0; i < segments.size(); ++i) put16(sub, 0);
    QByteArray table;
    put16(table, 0);
    put16(table, 1);
    put16(table, 3);
    put16(table, 1);
    put32(table, 12);
    return table + sub;
}

}

namespace CffFont {

bool isBare(const QByteArray &data)
{
    return data.size() > 4 && quint8(data.at(0)) == 1 && quint8(data.at(1)) == 0;
}

QList<QPair<QByteArray, QByteArray>> tables(const QByteArray &cff, bool bold)
{
    if (!isBare(cff)) return {};
    const qsizetype headerSize = quint8(cff.at(2));
    const std::optional<Index> names = readIndex(cff, headerSize);
    if (!names) return {};
    const std::optional<Index> top = readIndex(cff, names->end);
    if (!top || top->count < 1) return {};
    qsizetype charStrings = -1;
    QList<double> box = { 0, -200, 1000, 800 };
    for (const auto &[op, operands] : readDict(cff, top->dataStart + top->offsets.at(0),
                                               top->dataStart + top->offsets.at(1))) {
        if (op == 1230) return {};                  // CID-keyed: no glyph names to map by
        if (op == 17 && !operands.isEmpty()) charStrings = qsizetype(operands.last());
        if (op == 5 && operands.size() == 4) box = operands;
    }
    const std::optional<Index> glyphs = charStrings > 0 ? readIndex(cff, charStrings) : std::nullopt;
    if (!glyphs || glyphs->count < 2) return {};
    const quint32 count = quint32(glyphs->count);

    const QRawFont probe(cff, 12.0);
    if (!probe.isValid()) return {};
    const qreal unitsPerEm = probe.unitsPerEm() > 0 ? probe.unitsPerEm() : 1000.0;
    const QRawFont raw(cff, unitsPerEm);

    QList<QPair<quint32, quint32>> map;
    for (quint32 code = 0x20; code < 0xFFFE; ++code) {
        if (code >= 0xD800 && code <= 0xDFFF) continue;
        const QList<quint32> ids = raw.glyphIndexesForString(QString(QChar(char16_t(code))));
        if (!ids.isEmpty() && ids.first() != 0 && ids.first() < count) map.append({ code, ids.first() });
    }
    if (map.isEmpty()) return {};

    QList<quint32> all;
    for (quint32 g = 0; g < count; ++g) all.append(g);
    const QList<QPointF> advances = raw.advancesForGlyphIndexes(all, QRawFont::UseDesignMetrics);
    quint32 widest = 0;
    QByteArray hmtx;
    for (quint32 g = 0; g < count; ++g) {
        const quint32 w = quint32(qMax(0.0, std::round(advances.value(int(g)).x())));
        widest = qMax(widest, w);
        put16(hmtx, w);
        put16(hmtx, 0);
    }
    const int ascent = int(std::round(raw.ascent())), descent = -int(std::round(raw.descent()));
    const auto s16 = [](int v) { return quint32(quint16(qint16(v))); };

    QByteArray head;
    put32(head, 0x00010000);
    put32(head, 0x00010000);
    put32(head, 0);
    put32(head, 0x5F0F3CF5);
    put16(head, 3);
    put16(head, quint32(unitsPerEm));
    for (int i = 0; i < 4; ++i) put32(head, 0);
    for (const double v : box) put16(head, s16(int(v)));
    put16(head, bold ? 1 : 0);
    put16(head, 8);
    put16(head, 2);
    put16(head, 0);
    put16(head, 0);

    QByteArray hhea;
    put32(hhea, 0x00010000);
    put16(hhea, s16(ascent));
    put16(hhea, s16(descent));
    put16(hhea, 0);
    put16(hhea, widest);
    put16(hhea, 0);
    put16(hhea, 0);
    put16(hhea, s16(int(box.at(2))));
    put16(hhea, 1);
    put16(hhea, 0);
    put16(hhea, 0);
    for (int i = 0; i < 4; ++i) put16(hhea, 0);
    put16(hhea, 0);
    put16(hhea, count);

    QByteArray maxp;
    put32(maxp, 0x00005000);
    put16(maxp, count);

    QByteArray os2;
    put16(os2, 4);
    put16(os2, s16(int(widest / 2)));
    put16(os2, bold ? 700 : 400);
    put16(os2, 5);
    put16(os2, 0);
    for (int i = 0; i < 11; ++i) put16(os2, 0);
    for (int i = 0; i < 10; ++i) os2.append('\0');
    put32(os2, 1);
    for (int i = 0; i < 3; ++i) put32(os2, 0);
    os2.append("OPD ", 4);
    put16(os2, bold ? 0x20 : 0x40);
    put16(os2, map.first().first);
    put16(os2, qMin<quint32>(map.last().first, 0xFFFF));
    put16(os2, s16(ascent));
    put16(os2, s16(descent));
    put16(os2, 0);
    put16(os2, quint32(qMax(0, ascent)));
    put16(os2, quint32(qMax(0, -descent)));
    put32(os2, 1);
    put32(os2, 0);
    put16(os2, s16(int(ascent * 0.5)));
    put16(os2, s16(int(ascent * 0.7)));
    put16(os2, 0);
    put16(os2, 32);
    put16(os2, 1);

    QByteArray post;
    put32(post, 0x00030000);
    put32(post, 0);
    put16(post, s16(-100));
    put16(post, 50);
    for (int i = 0; i < 5; ++i) put32(post, 0);

    return { { QByteArrayLiteral("CFF "), cff }, { QByteArrayLiteral("OS/2"), os2 },
             { QByteArrayLiteral("cmap"), cmapTable(map) }, { QByteArrayLiteral("head"), head },
             { QByteArrayLiteral("hhea"), hhea }, { QByteArrayLiteral("hmtx"), hmtx },
             { QByteArrayLiteral("maxp"), maxp }, { QByteArrayLiteral("post"), post } };
}

}
