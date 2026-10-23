#include "engine/edit/EditCodec.hpp"

#include <QDataStream>
#include <QIODevice>

namespace EditCodec {

namespace {

constexpr quint8 kVersion = 1;

void write(QDataStream &out, const TextBoxProperties &b)
{
    out << b.bounds << b.autoHeight << b.paddingPt << qint32(b.verticalAlign)
        << qint32(b.horizontalAlign) << qint32(b.listStyle) << qint32(b.indentLevel)
        << b.rotationDeg << b.characterSpacingPt << b.paragraphSpacingPt
        << b.lineSpacingMultiplier << b.opacity << b.cornerRadiusPt << b.borderEnabled
        << qint32(b.borderStyle) << b.borderWidthPt << b.borderColor << b.backgroundEnabled
        << b.backgroundColor;
}

void read(QDataStream &in, TextBoxProperties &b)
{
    qint32 vertical = 0, horizontal = 0, list = 0, indent = 0, border = 0;
    in >> b.bounds >> b.autoHeight >> b.paddingPt >> vertical >> horizontal >> list >> indent
       >> b.rotationDeg >> b.characterSpacingPt >> b.paragraphSpacingPt
       >> b.lineSpacingMultiplier >> b.opacity >> b.cornerRadiusPt >> b.borderEnabled
       >> border >> b.borderWidthPt >> b.borderColor >> b.backgroundEnabled
       >> b.backgroundColor;
    b.verticalAlign   = TextBoxProperties::VerticalAlign(vertical);
    b.horizontalAlign = TextBoxProperties::HorizontalAlign(horizontal);
    b.listStyle       = TextBoxProperties::ListStyle(list);
    b.indentLevel     = indent;
    b.borderStyle     = TextBoxProperties::BorderStyle(border);
}

void write(QDataStream &out, const EditSession::Edit &e)
{
    out << qint32(e.page) << qint32(e.sourcePage) << e.pdfBounds << e.sourceRect << e.newText
        << e.fontSizePt << e.textOriginOffset << e.hasTextOrigin << e.lineSpacingPt
        << e.originalText << e.textColor << e.bgColor << e.fontFamily << e.bold << e.italic
        << e.underline << e.familyChanged << e.boldChanged << e.italicChanged << e.sizeChanged
        << e.baseSizePt << e.colorChanged << e.laid << e.scanned << e.formField << e.eraseRects;
    write(out, e.box);
    out << qint32(e.originalLines.size());
    for (const TextLayout::OriginalLine &l : e.originalLines)
        out << l.rect << l.text << l.baseline << l.charX << l.objectStart << l.endX;
    out << qint32(e.sources.size());
    for (const TextLayout::Source &s : e.sources)
        out << qint32(s.page) << s.at << s.rise;
}

bool read(QDataStream &in, EditSession::Edit &e)
{
    qint32 page = -1, sourcePage = -1;
    in >> page >> sourcePage >> e.pdfBounds >> e.sourceRect >> e.newText
       >> e.fontSizePt >> e.textOriginOffset >> e.hasTextOrigin >> e.lineSpacingPt
       >> e.originalText >> e.textColor >> e.bgColor >> e.fontFamily >> e.bold >> e.italic
       >> e.underline >> e.familyChanged >> e.boldChanged >> e.italicChanged >> e.sizeChanged
       >> e.baseSizePt >> e.colorChanged >> e.laid >> e.scanned >> e.formField >> e.eraseRects;
    e.page       = page;
    e.sourcePage = sourcePage;
    read(in, e.box);
    qint32 lines = 0;
    in >> lines;
    if (lines < 0) return false;
    for (qint32 i = 0; i < lines && in.status() == QDataStream::Ok; ++i) {
        TextLayout::OriginalLine l;
        in >> l.rect >> l.text >> l.baseline >> l.charX >> l.objectStart >> l.endX;
        e.originalLines.append(std::move(l));
    }
    qint32 sources = 0;
    in >> sources;
    if (sources < 0) return false;
    for (qint32 i = 0; i < sources && in.status() == QDataStream::Ok; ++i) {
        TextLayout::Source s;
        qint32 sourcePageOf = -1;
        in >> sourcePageOf >> s.at >> s.rise;
        s.page = sourcePageOf;
        e.sources.append(s);
    }
    return in.status() == QDataStream::Ok;
}

void write(QDataStream &out, const EditSession::DrawStroke &s)
{
    out << qint32(s.page) << s.points << s.color << s.widthPt;
}

bool read(QDataStream &in, EditSession::DrawStroke &s)
{
    qint32 page = -1;
    in >> page >> s.points >> s.color >> s.widthPt;
    s.page = page;
    return in.status() == QDataStream::Ok;
}

void write(QDataStream &out, const EditSession::ImageChange &c)
{
    out << qint32(c.page) << c.key << c.from << c.to << qint32(c.quarterTurns)
        << c.replacement << c.removed;
}

bool read(QDataStream &in, EditSession::ImageChange &c)
{
    qint32 page = -1, turns = 0;
    in >> page >> c.key >> c.from >> c.to >> turns >> c.replacement >> c.removed;
    c.page         = page;
    c.quarterTurns = turns;
    return in.status() == QDataStream::Ok;
}

template <typename T>
QByteArray encodeList(const QList<T> &items)
{
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << kVersion << qint32(items.size());
    for (const T &item : items) write(out, item);
    return data;
}

template <typename T>
bool decodeList(const QByteArray &data, QList<T> *items)
{
    QDataStream in(data);
    in.setVersion(QDataStream::Qt_6_0);
    quint8 version = 0;
    qint32 count = 0;
    in >> version >> count;
    if (version != kVersion || count < 0) return false;
    QList<T> out;
    for (qint32 i = 0; i < count; ++i) {
        T item;
        if (!read(in, item)) return false;
        out.append(std::move(item));
    }
    *items = std::move(out);
    return true;
}

}

QByteArray encode(const QList<EditSession::Edit> &edits) { return encodeList(edits); }
bool decode(const QByteArray &data, QList<EditSession::Edit> *edits) { return decodeList(data, edits); }

QByteArray encode(const QList<EditSession::DrawStroke> &strokes) { return encodeList(strokes); }
bool decode(const QByteArray &data, QList<EditSession::DrawStroke> *strokes)
{
    return decodeList(data, strokes);
}

QByteArray encode(const QList<EditSession::ImageChange> &changes) { return encodeList(changes); }
bool decode(const QByteArray &data, QList<EditSession::ImageChange> *changes)
{
    return decodeList(data, changes);
}

}
