#include "engine/export/PdfAFonts.hpp"

#ifdef HAVE_QPDF

#include "engine/edit/StandardFont.hpp"
#include "engine/export/GlyphNames.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QFont>
#include <QHash>
#include <QRawFont>

#include <array>
#include <cmath>
#include <map>
#include <vector>

namespace {

struct Encoding {
    std::array<char32_t, 256> unicode {};
    QPDFObjectHandle          object;
    QString                   problem;
};

Encoding encodingFor(QPDFObjectHandle font, const std::set<int> &used)
{
    Encoding out;
    QPDFObjectHandle enc = font.getKey("/Encoding");
    std::string base = enc.isName() ? enc.getName() : "";
    if (enc.isDictionary() && enc.getKey("/BaseEncoding").isName())
        base = enc.getKey("/BaseEncoding").getName();
    if (base == "/MacExpertEncoding" || (!base.empty() && base != "/WinAnsiEncoding"
                                         && base != "/MacRomanEncoding" && base != "/StandardEncoding")) {
        out.problem = QCoreApplication::translate("PdfA", "it uses an encoding PDF/A cannot keep");
        return out;
    }
    const bool mac = base == "/MacRomanEncoding";
    const GlyphNames::Names win = GlyphNames::winAnsi();
    const GlyphNames::Names names = base == "/WinAnsiEncoding" ? win : GlyphNames::standard();

    std::map<int, QByteArray> differences;
    QPDFObjectHandle diffs = enc.isDictionary() ? enc.getKey("/Differences")
                                                : QPDFObjectHandle::newNull();
    if (diffs.isArray()) {
        int code = -1;
        for (const QPDFObjectHandle &item : diffs.getArrayAsVector()) {
            if (item.isInteger()) code = int(item.getIntValue());
            else if (item.isName() && code >= 0 && code < 256)
                differences[code++] = QByteArray::fromStdString(item.getName().substr(1));
        }
    }

    QPDFObjectHandle written = QPDFObjectHandle::newArray();
    int last = -2;
    for (int c = 0; c < 256; ++c) {
        const auto diff = differences.find(c);
        QByteArray name;
        if (diff != differences.end()) {
            name = diff->second;
            out.unicode[size_t(c)] = GlyphNames::unicodeOf(name);
        } else if (mac) {
            out.unicode[size_t(c)] = c > 0x20 && c != 0x7F ? GlyphNames::macRomanUnicode(c) : 0;
            if (out.unicode[size_t(c)] == 0 && used.count(c)) {
                out.problem = QCoreApplication::translate("PdfA", "it shows characters its encoding leaves "
                                                                  "undefined");
                return out;
            }
            continue;
        } else {
            name = names[size_t(c)];
            out.unicode[size_t(c)] = GlyphNames::unicodeOf(name);
        }
        if (name.isEmpty()) {
            if (!used.count(c)) continue;
            // WinAnsiEncoding draws its unused codes above 0x20 as a bullet.
            if (base != "/WinAnsiEncoding" || c <= 0x20) {
                out.problem = QCoreApplication::translate("PdfA", "it shows characters its encoding leaves "
                                                                  "undefined");
                return out;
            }
            name = "bullet";
            out.unicode[size_t(c)] = 0x2022;
        }
        if (out.unicode[size_t(c)] == 0) {
            if (used.count(c)) {
                out.problem = QCoreApplication::translate("PdfA", "it names characters PDF/A cannot identify");
                return out;
            }
            continue;
        }
        if (diff == differences.end() && name == win[size_t(c)]) continue;
        if (c != last + 1) written.appendItem(QPDFObjectHandle::newInteger(c));
        written.appendItem(QPDFObjectHandle::newName("/" + name.toStdString()));
        last = c;
    }

    const std::string target = mac ? "/MacRomanEncoding" : "/WinAnsiEncoding";
    if (written.getArrayNItems() == 0) {
        out.object = QPDFObjectHandle::newName(target);
    } else {
        out.object = QPDFObjectHandle::newDictionary();
        out.object.replaceKey("/Type", QPDFObjectHandle::newName("/Encoding"));
        out.object.replaceKey("/BaseEncoding", QPDFObjectHandle::newName(target));
        out.object.replaceKey("/Differences", written);
    }
    return out;
}

struct Wanted {
    QString shown;
    QString family;
    bool    bold   { false };
    bool    italic { false };
    bool    symbol { false };
};

Wanted wantedFor(QPDFObjectHandle font)
{
    Wanted w;
    QPDFObjectHandle baseFont = font.getKey("/BaseFont");
    QString base = baseFont.isName() ? QString::fromStdString(baseFont.getName().substr(1)) : QString();
    if (base.size() > 7 && base.at(6) == u'+') base = base.mid(7);
    w.shown = base;
    const QString lower = base.toLower();
    w.bold = lower.contains(QLatin1String("bold")) || lower.contains(QLatin1String("black"))
          || lower.contains(QLatin1String("heavy"));
    w.italic = lower.contains(QLatin1String("italic")) || lower.contains(QLatin1String("oblique"));
    QPDFObjectHandle descriptor = font.getKey("/FontDescriptor");
    if (descriptor.isDictionary()) {
        QPDFObjectHandle weight = descriptor.getKey("/FontWeight");
        QPDFObjectHandle flags = descriptor.getKey("/Flags");
        QPDFObjectHandle angle = descriptor.getKey("/ItalicAngle");
        if (weight.isNumber() && weight.getNumericValue() >= 600) w.bold = true;
        if (flags.isInteger() && (flags.getIntValue() & (1 << 18))) w.bold = true;
        if (angle.isNumber() && angle.getNumericValue() != 0) w.italic = true;
    }

    QString family = base.section(u',', 0, 0).section(u'-', 0, 0);
    for (const char *suffix : { "PSMT", "PS", "MT" })
        if (family.endsWith(QLatin1String(suffix)) && family.size() > int(qstrlen(suffix)))
            family.chop(int(qstrlen(suffix)));
    for (int i = family.size() - 1; i > 0; --i)
        if (family.at(i).isUpper() && family.at(i - 1).isLower()) family.insert(i, u' ');
    w.family = family;
    const QString plain = family.toLower().remove(u' ');
    w.symbol = plain == QLatin1String("symbol") || plain == QLatin1String("zapfdingbats");
    return w;
}

QStringList candidatesFor(const QString &family)
{
    const QString plain = family.toLower().remove(u' ');
    const auto in = [&plain](std::initializer_list<const char *> names) {
        for (const char *name : names) if (plain == QLatin1String(name)) return true;
        return false;
    };
    QStringList out { family };
    // Families drawn to the same character widths as the font a PDF names.
    if (in({ "helvetica", "arial", "liberationsans", "arimo", "nimbussans" }))
        out << QStringLiteral("Liberation Sans") << QStringLiteral("Arimo")
            << QStringLiteral("Nimbus Sans") << QStringLiteral("Arial");
    else if (in({ "times", "timesnewroman", "liberationserif", "tinos", "nimbusroman" }))
        out << QStringLiteral("Liberation Serif") << QStringLiteral("Tinos")
            << QStringLiteral("Nimbus Roman") << QStringLiteral("Times New Roman");
    else if (in({ "courier", "couriernew", "liberationmono", "cousine", "nimbusmonops" }))
        out << QStringLiteral("Liberation Mono") << QStringLiteral("Cousine")
            << QStringLiteral("Nimbus Mono PS") << QStringLiteral("Courier New");
    else if (in({ "calibri", "carlito" }))
        out << QStringLiteral("Calibri") << QStringLiteral("Carlito");
    else if (in({ "cambria", "caladea" }))
        out << QStringLiteral("Cambria") << QStringLiteral("Caladea");
    out.removeDuplicates();
    return out;
}

quint16 u16(const QByteArray &d, qsizetype at)
{
    return at + 2 <= d.size() ? quint16((quint8(d.at(at)) << 8) | quint8(d.at(at + 1))) : 0;
}

qint16 s16(const QByteArray &d, qsizetype at) { return qint16(u16(d, at)); }

quint32 u32(const QByteArray &d, qsizetype at) { return (quint32(u16(d, at)) << 16) | u16(d, at + 2); }

void put16(QByteArray &out, quint16 v) { out.append(char(v >> 8)).append(char(v & 0xFF)); }

void put32(QByteArray &out, quint32 v)
{
    put16(out, quint16(v >> 16));
    put16(out, quint16(v & 0xFFFF));
}

QString plainName(const QString &family) { return family.toLower().remove(u' '); }

bool hasWindowsUnicodeCmap(const QByteArray &cmap)
{
    for (int i = 0; i < u16(cmap, 2); ++i)
        if (u16(cmap, 4 + 8 * i) == 3 && u16(cmap, 6 + 8 * i) == 1) return true;
    return false;
}

QRawFont faceFor(const QString &family, bool bold, bool italic)
{
    QFont request(family);
    request.setBold(bold);
    request.setItalic(italic);
    request.setStyleStrategy(QFont::NoFontMerging);
    const QRawFont raw = QRawFont::fromFont(request);
    if (!raw.isValid() || plainName(raw.familyName()) != plainName(family)) return {};
    if ((raw.weight() >= 600) != bold || (raw.style() != QFont::StyleNormal) != italic) return {};
    for (const char *tag : { "glyf", "loca", "head", "hhea", "hmtx", "maxp", "cmap" })
        if (raw.fontTable(tag).isEmpty()) return {};
    // A variable font hands out only its default instance.
    if (!raw.fontTable("fvar").isEmpty() || !hasWindowsUnicodeCmap(raw.fontTable("cmap")))
        return {};
    const quint16 fsType = u16(raw.fontTable("OS/2"), 8);
    if ((fsType & 0x000F) == 0x0002 || (fsType & 0x0200)) return {};
    return raw;
}

quint32 glyphFor(const QRawFont &raw, char32_t unicode)
{
    if (!unicode) return 0;
    const QList<quint32> glyphs = raw.glyphIndexesForString(QString::fromUcs4(&unicode, 1));
    return glyphs.size() == 1 ? glyphs.first() : 0;
}

int widthOf(const QRawFont &raw, quint32 glyph)
{
    const QByteArray hhea = raw.fontTable("hhea"), hmtx = raw.fontTable("hmtx");
    const int metrics = u16(hhea, 34);
    if (metrics == 0) return 0;
    const quint16 advance = u16(hmtx, 4 * qsizetype(qMin(int(glyph), metrics - 1)));
    const int unitsPerEm = qMax(16, int(u16(raw.fontTable("head"), 18)));
    return int(std::lround(advance * 1000.0 / unitsPerEm));
}

// Glyphs not kept are emptied rather than removed, so glyph numbers stay as they are.
QByteArray subsetProgram(const QRawFont &raw, std::set<quint32> keep)
{
    QByteArray head = raw.fontTable("head");
    const QByteArray loca = raw.fontTable("loca"), glyf = raw.fontTable("glyf");
    const quint32 count = u16(raw.fontTable("maxp"), 4);
    const bool longLoca = s16(head, 50) == 1;
    const auto offset = [&](quint32 g) {
        return longLoca ? u32(loca, 4 * qsizetype(g)) : 2u * u16(loca, 2 * qsizetype(g));
    };

    keep.insert(0);
    std::vector<quint32> queue(keep.begin(), keep.end());
    while (!queue.empty()) {
        const quint32 g = queue.back();
        queue.pop_back();
        if (g >= count) continue;
        const quint32 start = offset(g), end = offset(g + 1);
        if (end <= start || end > quint32(glyf.size()) || s16(glyf, start) >= 0) continue;
        qsizetype at = start + 10;
        for (quint16 flags = 0x20; (flags & 0x20) && at + 4 <= end;) {
            flags = u16(glyf, at);
            const quint32 component = u16(glyf, at + 2);
            if (keep.insert(component).second) queue.push_back(component);
            at += 4 + ((flags & 0x1) ? 4 : 2) + ((flags & 0x8) ? 2 : 0) + ((flags & 0x40) ? 4 : 0)
                + ((flags & 0x80) ? 8 : 0);
        }
    }

    QByteArray newGlyf, newLoca;
    for (quint32 g = 0; g < count; ++g) {
        put32(newLoca, quint32(newGlyf.size()));
        const quint32 start = offset(g), end = offset(g + 1);
        if (!keep.count(g) || end <= start || end > quint32(glyf.size())) continue;
        newGlyf.append(glyf.mid(start, end - start));
        newGlyf.append(QByteArray((4 - newGlyf.size() % 4) % 4, '\0'));
    }
    put32(newLoca, quint32(newGlyf.size()));
    head[50] = 0;
    head[51] = 1;
    for (int i = 8; i < 12; ++i) head[i] = 0;

    QList<QPair<QByteArray, QByteArray>> tables;
    for (const char *tag : { "OS/2", "cmap", "cvt ", "fpgm", "glyf", "head", "hhea", "hmtx", "loca",
                             "maxp", "name", "post", "prep" }) {
        const QByteArray name(tag);
        const QByteArray data = name == "glyf" ? newGlyf : name == "loca" ? newLoca
                              : name == "head" ? head : raw.fontTable(tag);
        if (!data.isEmpty()) tables.append({ name, data });
    }

    const int n = int(tables.size());
    int power = 1, log = 0;
    while (power * 2 <= n) { power *= 2; ++log; }
    QByteArray out;
    put32(out, 0x00010000);
    put16(out, quint16(n));
    put16(out, quint16(power * 16));
    put16(out, quint16(log));
    put16(out, quint16(n * 16 - power * 16));
    QByteArray body;
    quint32 at = quint32(12 + 16 * n);
    for (const auto &table : std::as_const(tables)) {
        QByteArray padded = table.second;
        padded.append(QByteArray((4 - padded.size() % 4) % 4, '\0'));
        quint32 sum = 0;
        for (qsizetype i = 0; i < padded.size(); i += 4) sum += u32(padded, i);
        out.append(table.first);
        put32(out, sum);
        put32(out, at);
        put32(out, quint32(table.second.size()));
        body.append(padded);
        at += quint32(padded.size());
    }
    return out + body;
}

QString postScriptName(const QRawFont &raw)
{
    const QByteArray name = raw.fontTable("name");
    const int count = u16(name, 2), strings = u16(name, 4);
    for (int i = 0; i < count; ++i) {
        const qsizetype at = 6 + 12 * qsizetype(i);
        if (u16(name, at) != 3 || u16(name, at + 2) != 1 || u16(name, at + 6) != 6) continue;
        const int length = u16(name, at + 8), offset = u16(name, at + 10);
        QString out;
        for (int c = 0; c + 1 < length; c += 2) out += QChar(u16(name, strings + offset + c));
        if (!out.isEmpty()) return out;
    }
    QString out = raw.familyName() + u'-' + raw.styleName();
    return out.remove(u' ');
}

QPDFObjectHandle descriptorFor(QPDF &pdf, const QRawFont &raw, const QByteArray &program,
                               const std::string &fontName, bool serif)
{
    const QByteArray head = raw.fontTable("head"), hhea = raw.fontTable("hhea");
    const QByteArray os2 = raw.fontTable("OS/2"), post = raw.fontTable("post");
    const double scale = 1000.0 / qMax(16, int(u16(head, 18)));
    const auto scaled = [scale](int v) { return QPDFObjectHandle::newInteger(std::lround(v * scale)); };
    const int ascent = s16(hhea, 4);
    const double italicAngle = qint32(u32(post, 4)) / 65536.0;
    const bool fixed = u32(post, 12) != 0;
    const bool italic = raw.style() != QFont::StyleNormal;

    QPDFObjectHandle bbox = QPDFObjectHandle::newArray();
    for (int at : { 36, 38, 40, 42 }) bbox.appendItem(scaled(s16(head, at)));
    QPDFObjectHandle file = pdf.newStream(std::string(program.constData(), size_t(program.size())));
    file.getDict().replaceKey("/Length1", QPDFObjectHandle::newInteger(program.size()));

    QPDFObjectHandle d = QPDFObjectHandle::newDictionary();
    d.replaceKey("/Type", QPDFObjectHandle::newName("/FontDescriptor"));
    d.replaceKey("/FontName", QPDFObjectHandle::newName("/" + fontName));
    d.replaceKey("/Flags", QPDFObjectHandle::newInteger(32 | (fixed ? 1 : 0) | (serif ? 2 : 0)
                                                       | (italic ? 64 : 0)));
    d.replaceKey("/FontBBox", bbox);
    d.replaceKey("/ItalicAngle", QPDFObjectHandle::newReal(italicAngle, 2));
    d.replaceKey("/Ascent", scaled(ascent));
    d.replaceKey("/Descent", scaled(s16(hhea, 6)));
    d.replaceKey("/CapHeight", u16(os2, 0) >= 2 && os2.size() >= 90 ? scaled(s16(os2, 88))
                                                                    : scaled(ascent * 7 / 10));
    d.replaceKey("/StemV", QPDFObjectHandle::newInteger(raw.weight() >= 600 ? 140 : 80));
    d.replaceKey("/FontFile2", file);
    return pdf.makeIndirectObject(d);
}

bool isEmbedded(QPDFObjectHandle font)
{
    QPDFObjectHandle subtype = font.getKey("/Subtype");
    if (subtype.isName() && subtype.getName() == "/Type3") return true;
    QPDFObjectHandle descriptor = font.getKey("/FontDescriptor");
    if (subtype.isName() && subtype.getName() == "/Type0") {
        QPDFObjectHandle descendants = font.getKey("/DescendantFonts");
        if (descendants.isArray() && descendants.getArrayNItems() > 0)
            descriptor = descendants.getArrayItem(0).getKey("/FontDescriptor");
    }
    return descriptor.isDictionary() && (descriptor.hasKey("/FontFile")
                                         || descriptor.hasKey("/FontFile2")
                                         || descriptor.hasKey("/FontFile3"));
}

bool isStandard14(QPDFObjectHandle font)
{
    static const std::set<std::string> names {
        "/Courier", "/Courier-Bold", "/Courier-Oblique", "/Courier-BoldOblique", "/Helvetica",
        "/Helvetica-Bold", "/Helvetica-Oblique", "/Helvetica-BoldOblique", "/Times-Roman",
        "/Times-Bold", "/Times-Italic", "/Times-BoldItalic", "/Symbol", "/ZapfDingbats" };
    QPDFObjectHandle base = font.getKey("/BaseFont");
    return font.getKey("/Subtype").isName() && font.getKey("/Subtype").getName() == "/Type1"
        && base.isName() && names.count(base.getName());
}

// Text drawn invisibly needs no font program, but its font dictionary must still be complete.
void completeDictionary(QPDF &pdf, QPDFObjectHandle font, const Wanted &wanted, PdfA::Report &report)
{
    if (isStandard14(font)) return;
    QPDFObjectHandle first = font.getKey("/FirstChar"), last = font.getKey("/LastChar");
    QPDFObjectHandle widths = font.getKey("/Widths");
    if (!first.isInteger() || !last.isInteger() || !widths.isArray()
            || widths.getArrayNItems() != last.getIntValue() - first.getIntValue() + 1) {
        const Encoding encoding = encodingFor(font, {});
        QRawFont raw;
        const QStringList candidates = candidatesFor(wanted.family) << QStringLiteral("Liberation Sans");
        for (const QString &candidate : candidates) {
            raw = faceFor(candidate, wanted.bold, wanted.italic);
            if (raw.isValid()) break;
        }
        QPDFObjectHandle list = QPDFObjectHandle::newArray();
        for (int c = 32; c < 256; ++c) {
            const quint32 glyph = raw.isValid() ? glyphFor(raw, encoding.unicode[size_t(c)]) : 0;
            list.appendItem(QPDFObjectHandle::newInteger(glyph ? widthOf(raw, glyph) : 500));
        }
        font.replaceKey("/FirstChar", QPDFObjectHandle::newInteger(32));
        font.replaceKey("/LastChar", QPDFObjectHandle::newInteger(255));
        font.replaceKey("/Widths", list);
        ++report.repairs;
    }
    if (!font.getKey("/FontDescriptor").isDictionary()) {
        QPDFObjectHandle d = QPDFObjectHandle::newDictionary();
        QPDFObjectHandle bbox = QPDFObjectHandle::newArray();
        for (int v : { 0, -200, 1000, 900 }) bbox.appendItem(QPDFObjectHandle::newInteger(v));
        d.replaceKey("/Type", QPDFObjectHandle::newName("/FontDescriptor"));
        d.replaceKey("/FontName", font.getKey("/BaseFont").isName() ? font.getKey("/BaseFont")
                                                                     : QPDFObjectHandle::newName("/Unnamed"));
        d.replaceKey("/Flags", QPDFObjectHandle::newInteger(32));
        d.replaceKey("/FontBBox", bbox);
        d.replaceKey("/ItalicAngle", QPDFObjectHandle::newInteger(0));
        d.replaceKey("/Ascent", QPDFObjectHandle::newInteger(900));
        d.replaceKey("/Descent", QPDFObjectHandle::newInteger(-200));
        d.replaceKey("/CapHeight", QPDFObjectHandle::newInteger(700));
        d.replaceKey("/StemV", QPDFObjectHandle::newInteger(80));
        font.replaceKey("/FontDescriptor", pdf.makeIndirectObject(d));
        ++report.repairs;
    }
}

QString subsetTag(const QString &seed)
{
    const QByteArray hash = QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha1);
    QString tag;
    for (int i = 0; i < 6; ++i) tag += QChar(u'A' + quint8(hash.at(i)) % 26);
    return tag;
}

}

void PdfAFonts::embed(QPDF &pdf, const PdfAContent::Use &use, PdfA::Report &report)
{
    for (const auto &entry : use.fonts) {
        const PdfAContent::FontUse &used = entry.second;
        QPDFObjectHandle font = used.font;
        if (!font.isDictionary()) continue;
        QPDFObjectHandle descendants = font.getKey("/DescendantFonts");
        QPDFObjectHandle cid = descendants.isArray() && descendants.getArrayNItems() > 0
                             ? descendants.getArrayItem(0) : QPDFObjectHandle::newNull();
        // The default PDF/A wants written out.
        if (cid.isDictionary() && cid.getKey("/Subtype").isName()
                && cid.getKey("/Subtype").getName() == "/CIDFontType2" && !cid.hasKey("/CIDToGIDMap")) {
            cid.replaceKey("/CIDToGIDMap", QPDFObjectHandle::newName("/Identity"));
            ++report.repairs;
        }
        if (isEmbedded(font)) continue;

        const Wanted wanted = wantedFor(font);
        const std::string kind = font.getKey("/Subtype").isName() ? font.getKey("/Subtype").getName() : "";
        if (!used.visible) {
            if (kind == "/Type1" || kind == "/MMType1" || kind == "/TrueType")
                completeDictionary(pdf, font, wanted, report);
            continue;
        }
        const QString shown = wanted.shown.isEmpty() ? QCoreApplication::translate("PdfA", "(unnamed)")
                                                     : wanted.shown;
        const std::string subtype = font.getKey("/Subtype").isName()
                                  ? font.getKey("/Subtype").getName() : "";
        if (subtype != "/Type1" && subtype != "/MMType1" && subtype != "/TrueType") {
            report.blockers << QCoreApplication::translate("PdfA", "The font \"%1\" is not embedded and "
                                                                   "cannot be replaced.").arg(shown);
            continue;
        }
        if (wanted.symbol) {
            report.blockers << QCoreApplication::translate("PdfA", "The font \"%1\" is not embedded and no "
                                                                   "installed font can stand in for it.")
                                   .arg(shown);
            continue;
        }
        const Encoding encoding = encodingFor(font, used.codes);
        if (!encoding.problem.isEmpty()) {
            report.blockers << QCoreApplication::translate("PdfA",
                "The font \"%1\" cannot be embedded: %2.").arg(shown, encoding.problem);
            continue;
        }

        QPDFObjectHandle widths = font.getKey("/Widths");
        QPDFObjectHandle first = font.getKey("/FirstChar");
        QRawFont chosen;
        bool missing = false;
        for (const QString &candidate : candidatesFor(wanted.family)) {
            const QRawFont raw = faceFor(candidate, wanted.bold, wanted.italic);
            if (!raw.isValid()) continue;
            bool fits = true;
            for (const int code : used.codes) {
                const quint32 glyph = glyphFor(raw, encoding.unicode[size_t(code)]);
                if (!glyph) {
                    missing = true;
                    fits = false;
                    break;
                }
                const int index = first.isInteger() ? code - int(first.getIntValue()) : -1;
                if (widths.isArray() && index >= 0 && index < widths.getArrayNItems()
                        && widths.getArrayItem(index).isNumber()
                        && std::abs(widths.getArrayItem(index).getNumericValue()
                                    - widthOf(raw, glyph)) > 2.0) {
                    fits = false;
                    break;
                }
            }
            if (fits) {
                chosen = raw;
                break;
            }
        }
        if (!chosen.isValid()) {
            report.blockers << (missing
                ? QCoreApplication::translate("PdfA", "The font \"%1\" is not embedded and no installed "
                                                      "font has all of its characters.")
                : QCoreApplication::translate("PdfA", "The font \"%1\" is not embedded and no installed "
                                                      "font has the same character widths."))
                                   .arg(shown);
            continue;
        }

        std::set<quint32> glyphs;
        int firstCode = -1, lastCode = -1;
        std::array<int, 256> advance {};
        for (int c = 0; c < 256; ++c) {
            const quint32 glyph = glyphFor(chosen, encoding.unicode[size_t(c)]);
            if (!glyph) continue;
            if (used.codes.count(c)) glyphs.insert(glyph);
            advance[size_t(c)] = widthOf(chosen, glyph);
            if (firstCode < 0) firstCode = c;
            lastCode = c;
        }
        if (firstCode < 0) firstCode = lastCode = 32;
        QPDFObjectHandle newWidths = QPDFObjectHandle::newArray();
        for (int c = firstCode; c <= lastCode; ++c)
            newWidths.appendItem(QPDFObjectHandle::newInteger(advance[size_t(c)]));

        const QByteArray program = subsetProgram(chosen, glyphs);
        const QString psName = postScriptName(chosen);
        const QString name = subsetTag(psName + QString::number(font.getObjectID())) + u'+' + psName;
        const bool serif = StandardFont::kindOf(chosen.familyName()) == StandardFont::Kind::Serif;
        font.replaceKey("/Subtype", QPDFObjectHandle::newName("/TrueType"));
        font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/" + name.toStdString()));
        font.replaceKey("/FirstChar", QPDFObjectHandle::newInteger(firstCode));
        font.replaceKey("/LastChar", QPDFObjectHandle::newInteger(lastCode));
        font.replaceKey("/Widths", newWidths);
        font.replaceKey("/Encoding", encoding.object);
        font.replaceKey("/FontDescriptor",
                        descriptorFor(pdf, chosen, program, name.toStdString(), serif));
        report.changes << QCoreApplication::translate("PdfA",
            "The font \"%1\" was embedded as %2.").arg(shown, chosen.familyName());
    }
}

#endif
