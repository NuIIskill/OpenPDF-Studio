#include "engine/document/PdfiumFonts.hpp"

#include "engine/document/CffFont.hpp"

#include "engine/edit/ContentMap.hpp"
#include "engine/edit/StandardFont.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "fpdf_edit.h"
#include "fpdf_text.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QMap>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QRawFont>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cstring>
#include <utility>

#include <vector>

namespace {

QHash<QByteArray, QString> &registry()
{
    static QHash<QByteArray, QString> cache;
    return cache;
}

QHash<QString, QString> &renamedFamilies()
{
    static QHash<QString, QString> names;
    return names;
}

// A family as the font names it: Qt adds the foundry, "Nimbus Sans [URW ]",
// where two foundries ship one family.
QString plainFamily(const QString &family)
{
    static const QRegularExpression foundry(QStringLiteral("\\s*\\[[^\\]]*\\]\\s*$"));
    return QString(family).remove(foundry);
}

QString writableFamily(const QString &family)
{
    // A document font registered under a name of its own is written in the
    // system font it was renamed from; the registered program is a subset.
    return renamedFamilies().value(family, family);
}

constexpr const char *kSfntTags[] = {
    "BASE", "CFF ", "DSIG", "GDEF", "GPOS", "GSUB", "LTSH", "MATH", "OS/2",
    "PCLT", "VDMX", "VORG", "cmap", "cvt ", "fpgm", "gasp", "glyf", "hdmx",
    "head", "hhea", "hmtx", "kern", "loca", "maxp", "name", "post", "prep",
    "vhea", "vmtx"
};

void put16(QByteArray &out, quint16 v)
{
    out.append(char(v >> 8)).append(char(v & 0xFF));
}

void put32(QByteArray &out, quint32 v)
{
    out.append(char(v >> 24)).append(char((v >> 16) & 0xFF));
    out.append(char((v >> 8) & 0xFF)).append(char(v & 0xFF));
}

quint32 tableSum(const QByteArray &table)
{
    quint32 sum = 0;
    for (int i = 0; i < table.size(); i += 4) {
        quint32 wort = 0;
        for (int b = 0; b < 4; ++b)
            wort = (wort << 8)
                 | (i + b < table.size() ? quint8(table.at(i + b)) : 0);
        sum += wort;
    }
    return sum;
}

using Tables = QList<QPair<QByteArray, QByteArray>>;

QByteArray assembleSfnt(Tables tabellen, bool cff)
{
    std::sort(tabellen.begin(), tabellen.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    const int anzahl = tabellen.size();
    int potenz = 1;
    while (potenz * 2 <= anzahl) potenz *= 2;
    int stellen = 0;
    for (int p = potenz; p > 1; p >>= 1) ++stellen;

    QByteArray kopf;
    put32(kopf, cff ? 0x4F54544F : 0x00010000);
    put16(kopf, quint16(anzahl));
    put16(kopf, quint16(potenz * 16));
    put16(kopf, quint16(stellen));
    put16(kopf, quint16((anzahl - potenz) * 16));

    QByteArray verzeichnis, rumpf;
    quint32 versatz = quint32(12 + 16 * anzahl);
    for (const auto &t : std::as_const(tabellen)) {
        verzeichnis.append(t.first);
        put32(verzeichnis, tableSum(t.second));
        put32(verzeichnis, versatz);
        put32(verzeichnis, quint32(t.second.size()));
        rumpf.append(t.second);
        const int fuellung = (4 - (t.second.size() % 4)) % 4;
        rumpf.append(QByteArray(fuellung, '\0'));
        versatz += quint32(t.second.size() + fuellung);
    }
    return kopf + verzeichnis + rumpf;
}

QByteArray buildSfnt(const QRawFont &raw)
{
    Tables tabellen;
    for (const char *tag : kSfntTags) {
        const QByteArray daten = raw.fontTable(tag);
        if (!daten.isEmpty()) tabellen.append({ QByteArray(tag), daten });
    }
    const auto hat = [&tabellen](const char *tag) {
        return std::any_of(tabellen.cbegin(), tabellen.cend(),
                           [tag](const auto &t) { return t.first == tag; });
    };
    const bool cff = hat("CFF ");
    if (!cff && !hat("glyf")) return {};
    return assembleSfnt(tabellen, cff);
}

QString postScriptName(const QString &family, const QString &style)
{
    QString out;
    for (const QChar c : family + u'-' + style)
        if (c.unicode() < 128 && (c.isLetterOrNumber() || c == u'-')) out += c;
    return out;
}

QByteArray nameTable(const QString &family, const QString &style, const QString &postScript)
{
    const QList<QPair<quint16, QString>> names = {
        { 1, family }, { 2, style }, { 4, family + u' ' + style }, { 6, postScript } };

    QByteArray records, strings;
    for (const auto &name : names) {
        QByteArray utf16;
        for (const QChar c : name.second) put16(utf16, c.unicode());
        put16(records, 3);
        put16(records, 1);
        put16(records, 0x409);
        put16(records, name.first);
        put16(records, quint16(utf16.size()));
        put16(records, quint16(strings.size()));
        strings += utf16;
    }
    QByteArray table;
    put16(table, 0);
    put16(table, quint16(names.size()));
    put16(table, quint16(6 + 12 * names.size()));
    return table + records + strings;
}

QByteArray renamedSfnt(const QByteArray &font, const QString &family, const QString &style,
                       const QString &postScript)
{
    const auto u16 = [&font](qsizetype at) {
        return quint16((quint8(font.at(at)) << 8) | quint8(font.at(at + 1)));
    };
    const auto u32 = [&u16](qsizetype at) { return (quint32(u16(at)) << 16) | u16(at + 2); };
    if (font.size() < 12) return {};
    const quint32 version = u32(0);
    if (version != 0x00010000 && version != 0x74727565 && version != 0x4F54544F) return {};
    const int count = u16(4);
    if (12 + 16 * qsizetype(count) > font.size()) return {};

    Tables tables;
    for (int i = 0; i < count; ++i) {
        const qsizetype at = 12 + 16 * qsizetype(i);
        const QByteArray tag = font.mid(at, 4);
        const quint32 offset = u32(at + 8), length = u32(at + 12);
        if (quint64(offset) + length > quint64(font.size())) return {};
        if (tag != "name") tables.append({ tag, font.mid(offset, length) });
    }
    tables.append({ QByteArrayLiteral("name"), nameTable(family, style, postScript) });
    return assembleSfnt(tables, version == 0x4F54544F);
}

const QRawFont &face(const QString &family, bool bold, bool italic)
{
    static QHash<QString, QRawFont> faces;
    const QString key = family + (bold ? QStringLiteral("|f") : QString())
                               + (italic ? QStringLiteral("|k") : QString());
    auto it = faces.find(key);
    if (it == faces.end()) {
        QFont f(family);
        f.setBold(bold);
        f.setItalic(italic);
        f.setStyleStrategy(QFont::NoFontMerging);
        it = faces.insert(key, QRawFont::fromFont(f));
    }
    return *it;
}

}

namespace {

QHash<QString, QString> &fontFiles()
{
    static QHash<QString, QString> files;
    static bool scanned = false;
    if (scanned) return files;
    scanned = true;
    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::FontsLocation);
    dirs << QStringLiteral("/usr/share/fonts") << QStringLiteral("/usr/local/share/fonts")
         << QDir::homePath() + QStringLiteral("/.fonts");
    for (const QString &dir : std::as_const(dirs)) {
        QDirIterator it(dir, { QStringLiteral("*.ttf"), QStringLiteral("*.otf") }, QDir::Files,
                        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            const QFileInfo info(it.next());
            files.insert(info.completeBaseName().toLower(), info.filePath());
        }
    }
    return files;
}

QString subsetTag(const QString &key)
{
    // Every font variant gets a prefix of its own, so two variants that
    // resolve to the same program still have different names. The prefix
    // differs from one run to the next too: a file saved before holds the
    // fonts of that run, and an edit of it must not add a font of that name.
    static const quint32 run = QRandomGenerator::system()->bounded(308915776u);
    static QHash<QString, int> tags;
    int n = tags.value(key, -1);
    if (n < 0) {
        n = int(tags.size());
        tags.insert(key, n);
    }
    QString tag;
    for (quint32 i = 0, v = (run + quint32(n) * 7919u) % 308915776u; i < 6; ++i, v /= 26)
        tag += QChar(u'A' + v % 26);
    return tag + u'+';
}

QRawFont staticFace(const QRawFont &variable, bool italic)
{
    // Qt embeds a variable font's tables, which hold only its default
    // instance; a bold instance needs the static file of the same style.
    QString base = plainFamily(variable.familyName()) + (italic ? QStringLiteral("-BoldItalic")
                                                   : QStringLiteral("-Bold"));
    base.remove(u' ');
    const QString path = fontFiles().value(base.toLower());
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) return {};
    const QRawFont candidate(file.readAll(), variable.pixelSize());
    const bool same = candidate.isValid() && candidate.fontTable("fvar").isEmpty()
        && plainFamily(candidate.familyName()).compare(plainFamily(variable.familyName()),
                                                       Qt::CaseInsensitive) == 0
        && (candidate.style() != QFont::StyleNormal) == italic && candidate.weight() >= 600;
    return same ? candidate : QRawFont();
}

}

QByteArray PdfiumFonts::fontData(const QString &family, bool bold, bool italic)
{
    const QString schluessel = family + (bold ? QStringLiteral("|f") : QString())
                                      + (italic ? QStringLiteral("|k") : QString());
    static QHash<QString, QByteArray> speicher;
    const auto da = speicher.constFind(schluessel);
    if (da != speicher.cend()) return *da;

    QByteArray daten;
    QRawFont raw = face(writableFamily(family), bold, italic);
    if (raw.isValid() && bold && !raw.fontTable("fvar").isEmpty()) raw = staticFace(raw, italic);
    if (raw.isValid()) {

        const QByteArray os2 = raw.fontTable("OS/2");
        const quint16 fsType = os2.size() >= 10
            ? quint16((quint8(os2.at(8)) << 8) | quint8(os2.at(9))) : 0;
        // PDFium's page writer tells fonts apart by their base name, so one
        // named like a font already in the document would be written as that
        // font; the subset prefix keeps ours apart.
        if ((fsType & 0x000F) != 0x0002)
            daten = renamedSfnt(buildSfnt(raw), plainFamily(raw.familyName()), raw.styleName(),
                                subsetTag(schluessel)
                                    + postScriptName(plainFamily(raw.familyName()),
                                                     raw.styleName()));
    }
    speicher.insert(schluessel, daten);
    return daten;
}

QByteArray PdfiumFonts::standardFontFor(const QString &family, bool bold, bool italic)
{
    switch (StandardFont::kindOf(family)) {
    case StandardFont::Kind::Mono:
        return bold ? (italic ? "Courier-BoldOblique" : "Courier-Bold")
                    : (italic ? "Courier-Oblique"     : "Courier");
    case StandardFont::Kind::Serif:
        return bold ? (italic ? "Times-BoldItalic" : "Times-Bold")
                    : (italic ? "Times-Italic"     : "Times-Roman");
    case StandardFont::Kind::Sans:
        break;
    }
    return bold ? (italic ? "Helvetica-BoldOblique" : "Helvetica-Bold")
                : (italic ? "Helvetica-Oblique"     : "Helvetica");
}

bool PdfiumFonts::isType3(FPDF_FONT font)
{
    size_t size = 0;
    return font && FPDFFont_GetIsEmbedded(font) == 1
        && FPDFFont_GetFontData(font, nullptr, 0, &size) && size == 0;
}

namespace {

QHash<FPDF_DOCUMENT, QHash<QString, FPDF_FONT>> &loadedFonts()
{
    static QHash<FPDF_DOCUMENT, QHash<QString, FPDF_FONT>> fonts;
    return fonts;
}

QHash<FPDF_FONT, QRawFont> &loadedFaces()
{
    static QHash<FPDF_FONT, QRawFont> faces;
    return faces;
}

QHash<FPDF_FONT, QMap<quint32, uint>> &writtenGlyphMap()
{
    static QHash<FPDF_FONT, QMap<quint32, uint>> glyphs;
    return glyphs;
}

const QRawFont *loadedFace(FPDF_FONT font)
{
    const auto it = loadedFaces().constFind(font);
    return it == loadedFaces().cend() ? nullptr : &*it;
}

bool hasOutline(const QRawFont &raw, char32_t codePoint)
{
    if (!raw.isValid() || !raw.supportsCharacter(uint(codePoint))) return false;
    const QList<quint32> ids = raw.glyphIndexesForString(QString::fromUcs4(&codePoint, 1));
    return ids.size() == 1 && ids.first() != 0 && !raw.pathForGlyph(ids.first()).isEmpty();
}

struct Outline {
    float width    { 0.f };
    int   segments { -1 };
};

bool lacksCharacterMap(FPDF_FONT font)
{
    // An embedded TrueType program without a cmap (Qt writes its subsets so)
    // gives PDFium no glyph for a new character; it would draw one from a
    // substitute font and write that font's glyph id, which the file lacks.
    if (FPDFFont_GetIsEmbedded(font) != 1) return false;
    size_t size = 0;
    if (!FPDFFont_GetFontData(font, nullptr, 0, &size) || size < 12) return false;
    char name[128] = {};
    FPDFFont_GetBaseFontName(font, name, sizeof(name));
    const QString key = QStringLiteral("%1|%2|%3").arg(quintptr(font)).arg(size)
                                                  .arg(QLatin1String(name));
    static QHash<QString, bool> known;
    const auto cached = known.constFind(key);
    if (cached != known.cend()) return *cached;

    std::vector<uint8_t> data(size);
    size_t written = 0;
    bool lacks = false;
    if (FPDFFont_GetFontData(font, data.data(), size, &written) && written >= 12) {
        const auto u16 = [&data](size_t at) { return quint16((data[at] << 8) | data[at + 1]); };
        const quint32 version = (quint32(u16(0)) << 16) | u16(2);
        const int tables = u16(4);
        bool glyf = false, cmap = false;
        for (int i = 0; i < tables && 12 + 16 * size_t(i) + 4 <= written; ++i) {
            const char *tag = reinterpret_cast<const char *>(data.data() + 12 + 16 * i);
            glyf = glyf || std::memcmp(tag, "glyf", 4) == 0;
            cmap = cmap || std::memcmp(tag, "cmap", 4) == 0;
        }
        lacks = (version == 0x00010000 || version == 0x74727565) && glyf && !cmap;
    }
    known.insert(key, lacks);
    return lacks;
}

Outline outlineOf(FPDF_FONT font, char32_t codePoint)
{
    Outline o;
    if (!FPDFFont_GetGlyphWidth(font, codePoint, 12.f, &o.width)) o.width = 0.f;
    FPDF_GLYPHPATH path = FPDFFont_GetGlyphPath(font, codePoint, 12.f);
    o.segments = path ? FPDFGlyphPath_CountGlyphSegments(path) : -1;
    return o;
}

}

bool PdfiumFonts::hasGlyph(FPDF_FONT font, char32_t codePoint)
{
    if (!font || isType3(font)) return false;
    if (const QRawFont *raw = loadedFace(font))
        return hasOutline(*raw, codePoint);
    if (QChar::requiresSurrogates(codePoint) || lacksCharacterMap(font)) return false;

    const Outline glyph = outlineOf(font, codePoint);
    if (glyph.width <= 0.f || glyph.segments <= 0) return false;
    const Outline missing = outlineOf(font, 0xFFFF);
    return !(missing.segments == glyph.segments
             && qFuzzyCompare(missing.width, glyph.width));
}

bool PdfiumFonts::isLoaded(FPDF_FONT font)
{
    return loadedFace(font) != nullptr;
}

namespace {

QHash<FPDF_DOCUMENT, QHash<QString, bool>> &decodeCache()
{
    static QHash<FPDF_DOCUMENT, QHash<QString, bool>> cache;
    return cache;
}

QString decodeKey(FPDF_FONT font, uint codePoint)
{
    char name[128] = {};
    FPDFFont_GetBaseFontName(font, name, sizeof(name));
    return QStringLiteral("%1|%2|%3").arg(quintptr(font)).arg(QLatin1String(name)).arg(codePoint);
}

}

QSet<uint> PdfiumFonts::decodable(FPDF_DOCUMENT doc, FPDF_PAGE page, FPDF_FONT font,
                                  const QList<uint> &codePoints)
{
    QSet<uint> out;
    if (!font) return out;
    if (isLoaded(font) || FPDFFont_GetIsEmbedded(font) != 1)
        return QSet<uint>(codePoints.cbegin(), codePoints.cend());
    if (!doc || !page) return out;
    QHash<QString, bool> &cache = decodeCache()[doc];
    QList<QPair<uint, FPDF_PAGEOBJECT>> probes;
    for (const uint cp : codePoints) {
        // A control character makes an empty probe, which PDFium's text page
        // cannot take apart.
        if (QChar::category(char32_t(cp)) == QChar::Other_Control) continue;
        const auto known = cache.constFind(decodeKey(font, cp));
        if (known != cache.cend()) {
            if (*known) out.insert(cp);
            continue;
        }
        FPDF_PAGEOBJECT probe = FPDFPageObj_CreateTextObj(doc, font, 12.f);
        if (!probe) continue;
        const char32_t point = cp;
        const std::u16string utf16 = QString::fromUcs4(&point, 1).toStdU16String();
        FPDFText_SetText(probe, reinterpret_cast<FPDF_WIDESTRING>(utf16.c_str()));
        FPDFPageObj_Transform(probe, 1, 0, 0, 1, -10000, -10000 - 30.0 * probes.size());
        FPDFPage_InsertObject(page, probe);
        probes.append({ cp, probe });
    }
    if (probes.isEmpty()) return out;

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    for (const auto &probe : std::as_const(probes)) {
        bool ok = false;
        if (textPage) {
            unsigned short buffer[8] = {};
            const unsigned long bytes =
                FPDFTextObj_GetText(probe.second, textPage, buffer, sizeof(buffer));
            const char32_t point = probe.first;
            ok = bytes > 2 && QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer))
                                  == QString::fromUcs4(&point, 1);
        }
        cache.insert(decodeKey(font, probe.first), ok);
        if (ok) out.insert(probe.first);
    }
    if (textPage) FPDFText_ClosePage(textPage);
    for (const auto &probe : std::as_const(probes))
        if (FPDFPage_RemoveObject(page, probe.second)) FPDFPageObj_Destroy(probe.second);
    return out;
}

double PdfiumFonts::glyphWidth(FPDF_FONT font, char32_t codePoint, double sizePt)
{
    if (!font) return 0.0;
    if (const QRawFont *raw = loadedFace(font)) {
        if (raw->pixelSize() <= 0.0) return 0.0;
        double width = 0.0;
        const QList<quint32> ids = raw->glyphIndexesForString(QString::fromUcs4(&codePoint, 1));
        for (const QPointF &a : raw->advancesForGlyphIndexes(ids, QRawFont::UseDesignMetrics))
            width += a.x();
        return width * sizePt / raw->pixelSize();
    }
    if (QChar::requiresSurrogates(codePoint)) return 0.0;
    float width = 0.f;
    return FPDFFont_GetGlyphWidth(font, codePoint, static_cast<float>(sizePt), &width)
        ? double(width) : 0.0;
}

void PdfiumFonts::setText(FPDF_PAGEOBJECT object, FPDF_FONT font, const QString &text)
{
    const QList<uint> points = text.toUcs4();
    const QRawFont *raw = loadedFace(font);
    if (raw) {
        // A font the editor embedded is written by glyph id from its own
        // program: PDFium maps some characters to another glyph and gets the
        // ToUnicode entries of shared glyphs wrong, so the ids are kept too.
        QMap<quint32, uint> &written = writtenGlyphMap()[font];
        for (const uint cp : points) {
            const char32_t point = cp;
            const QList<quint32> id = raw->glyphIndexesForString(QString::fromUcs4(&point, 1));
            if (!id.isEmpty() && id.first() != 0 && !written.contains(id.first()))
                written.insert(id.first(), cp);
        }
        const QList<quint32> ids = raw->glyphIndexesForString(text);
        const std::vector<uint32_t> codes(ids.cbegin(), ids.cend());
        FPDFText_SetCharcodes(object, codes.data(), codes.size());
        return;
    }
    const std::u16string utf16 = text.toStdU16String();
    FPDFText_SetText(object, reinterpret_cast<FPDF_WIDESTRING>(utf16.c_str()));
}

namespace {

QStringList fallbackCandidates(bool serif)
{
    QStringList preferred = serif
        ? QStringList{ "DejaVu Serif", "Liberation Serif", "Times New Roman",
                       "Noto Serif", "FreeSerif" }
        : QStringList{};
    preferred << QStringList{ "DejaVu Sans", "Liberation Sans", "Arial", "Noto Sans",
                              "Segoe UI", "Tahoma", "FreeSans", "Droid Sans Fallback",
                              "Microsoft YaHei", "SimSun", "MS Gothic", "Malgun Gothic",
                              "Arial Unicode MS" };
    for (const QString &family : QFontDatabase::families())
        if (!preferred.contains(family)) preferred << family;
    return preferred;
}

QString fallbackFamily(char32_t codePoint, bool serif, bool bold, bool italic)
{
    static QHash<quint64, QString> chosen;
    const quint64 key = quint64(codePoint) | (quint64(serif) << 32)
                      | (quint64(bold) << 33) | (quint64(italic) << 34);
    const auto known = chosen.constFind(key);
    if (known != chosen.cend()) return *known;

    QString family;
    for (const QString &candidate : fallbackCandidates(serif)) {
        if (!hasOutline(face(candidate, bold, italic), codePoint)) continue;
        if (PdfiumFonts::fontData(candidate, bold, italic).isEmpty()) continue;
        family = candidate;
        break;
    }
    chosen.insert(key, family);
    return family;
}

struct BaseName {
    QString family;
    bool    bold   { false };
    bool    italic { false };
    bool    named  { false };
};

BaseName parseBaseName(QString name)
{
    if (name.size() > 7 && name.at(6) == u'+') name = name.mid(7);
    int cut = 0;
    while (cut < name.size() && name.at(cut) != u'-' && name.at(cut) != u',') ++cut;
    BaseName out;
    out.family = name.left(cut);
    out.family.remove(QRegularExpression(QStringLiteral("(PSMT|PS|MT)$")));
    const QString style = name.mid(cut).toLower();
    out.named  = style.size() > 1;
    out.bold   = style.contains(QLatin1String("bold")) || style.contains(QLatin1String("black"))
              || style.contains(QLatin1String("heavy")) || style.contains(QLatin1String("demi"));
    out.italic = style.contains(QLatin1String("italic")) || style.contains(QLatin1String("oblique"))
              || style.endsWith(QLatin1String("it"));
    return out;
}

// Whether a font draws bold: its own program says so when it is embedded,
// a name that names its style next; the weight PDFium reports for a font
// without /FontWeight is guessed from its stems (625 for C059-Roman).
bool drawsBold(FPDF_FONT font, const BaseName &base)
{
    if (base.bold) return true;
    thread_local QHash<FPDF_FONT, std::pair<QString, bool>> known;
    const auto hit = known.constFind(font);
    if (hit != known.cend() && hit->first == base.family) return hit->second;
    bool bold = false;
    size_t size = 0;
    QRawFont raw;
    if (FPDFFont_GetIsEmbedded(font) && FPDFFont_GetFontData(font, nullptr, 0, &size) && size > 0) {
        QByteArray data(qsizetype(size), Qt::Uninitialized);
        if (FPDFFont_GetFontData(font, reinterpret_cast<uint8_t *>(data.data()), size, &size))
            raw = QRawFont(data, 12.0);
    }
    if (raw.isValid()) bold = raw.weight() >= QFont::DemiBold;
    else if (!base.named) bold = FPDFFont_GetWeight(font) >= 600;
    known.insert(font, { base.family, bold });
    return bold;
}

QString familyKey(const QString &family)
{
    QString key;
    for (const QChar c : plainFamily(family).toLower())
        if (c.isLetterOrNumber()) key += c;
    return key;
}

QString systemFamily(const QString &documentFamily)
{
    static QHash<QString, QString> installed;
    if (installed.isEmpty())
        // Without the foundry: a family split over two foundries keeps all its faces.
        for (const QString &family : QFontDatabase::families())
            installed.insert(familyKey(family), plainFamily(family));
    static const QHash<QString, QString> twins = {
        { QStringLiteral("arial"), QStringLiteral("liberationsans") },
        { QStringLiteral("helvetica"), QStringLiteral("liberationsans") },
        { QStringLiteral("arialnarrow"), QStringLiteral("liberationsansnarrow") },
        { QStringLiteral("timesnewroman"), QStringLiteral("liberationserif") },
        { QStringLiteral("times"), QStringLiteral("liberationserif") },
        { QStringLiteral("couriernew"), QStringLiteral("liberationmono") },
        { QStringLiteral("courier"), QStringLiteral("liberationmono") },
        { QStringLiteral("calibri"), QStringLiteral("carlito") },
        { QStringLiteral("cambria"), QStringLiteral("caladea") },
    };
    const QString key = familyKey(documentFamily);
    if (key.isEmpty()) return {};
    if (installed.contains(key)) return installed.value(key);
    return installed.value(twins.value(key));
}

}

FPDF_FONT PdfiumFonts::loadFont(FPDF_DOCUMENT doc, const QString &family, bool bold,
                                bool italic)
{
    if (!doc || family.isEmpty()) return nullptr;
    const QString key = family + (bold ? QStringLiteral("|f") : QString())
                               + (italic ? QStringLiteral("|k") : QString());
    QHash<QString, FPDF_FONT> &fonts = loadedFonts()[doc];
    const auto known = fonts.constFind(key);
    if (known != fonts.cend()) return *known;

    const QByteArray data = fontData(family, bold, italic);
    FPDF_FONT font = data.isEmpty() ? nullptr
        : FPDFText_LoadFont(doc, reinterpret_cast<const uint8_t *>(data.constData()),
                            static_cast<uint32_t>(data.size()), FPDF_FONT_TRUETYPE, 1);
    fonts.insert(key, font);
    if (font) loadedFaces().insert(font, QRawFont(data, 12.0));
    return font;
}

FPDF_FONT PdfiumFonts::embeddedCopy(FPDF_DOCUMENT doc, FPDF_FONT font)
{
    // A document font PDFium cannot write in: its own program, a bare CFF,
    // embedded anew under a name of its own, so the text keeps its design.
    if (!doc || !font || FPDFFont_GetIsEmbedded(font) != 1) return nullptr;
    size_t size = 0;
    if (!FPDFFont_GetFontData(font, nullptr, 0, &size) || size == 0) return nullptr;
    QByteArray program(qsizetype(size), Qt::Uninitialized);
    if (!FPDFFont_GetFontData(font, reinterpret_cast<uint8_t *>(program.data()), size, &size)
            || !CffFont::isBare(program))
        return nullptr;
    const QString key = QStringLiteral("cff|")
        + QString::fromLatin1(QCryptographicHash::hash(program, QCryptographicHash::Md5).toHex());
    QHash<QString, FPDF_FONT> &fonts = loadedFonts()[doc];
    const auto known = fonts.constFind(key);
    if (known != fonts.cend()) return *known;

    char name[128] = {};
    FPDFFont_GetBaseFontName(font, name, sizeof(name));
    const BaseName base = parseBaseName(QString::fromLatin1(name));
    const bool bold = drawsBold(font, base);
    Tables tables = CffFont::tables(program, bold);
    QByteArray data;
    if (!tables.isEmpty() && !base.family.isEmpty()) {
        const QString style = bold ? QStringLiteral("Bold") : QStringLiteral("Regular");
        tables.append({ QByteArrayLiteral("name"),
                        nameTable(base.family, style, subsetTag(key) + postScriptName(base.family, style)) });
        data = assembleSfnt(tables, true);
    }
    FPDF_FONT copy = data.isEmpty() ? nullptr
        : FPDFText_LoadFont(doc, reinterpret_cast<const uint8_t *>(data.constData()),
                            static_cast<uint32_t>(data.size()), FPDF_FONT_TRUETYPE, 1);
    fonts.insert(key, copy);
    if (copy) loadedFaces().insert(copy, QRawFont(data, 12.0));
    return copy;
}

FPDF_FONT PdfiumFonts::fallbackFont(FPDF_DOCUMENT doc, FPDF_FONT primary,
                                    char32_t codePoint)
{
    constexpr int kSerifFlag = 1 << 1;
    char name[128] = {};
    if (primary) FPDFFont_GetBaseFontName(primary, name, sizeof(name));
    const BaseName base = parseBaseName(QString::fromLatin1(name));
    const StandardFont::Kind kind = StandardFont::kindOf(base.family);
    const bool serif = primary && ((FPDFFont_GetFlags(primary) & kSerifFlag)
                                   || kind == StandardFont::Kind::Serif);
    const bool bold  = primary && drawsBold(primary, base);
    int angle = 0;
    const bool italic = primary && ((FPDFFont_GetItalicAngle(primary, &angle) && angle != 0)
                                    || base.italic);
    // A character the subset lacks is drawn in the same design where the
    // system has it, or in a font of the same kind and metrics.
    QStringList alike;
    if (const QString same = systemFamily(base.family); !same.isEmpty()) alike << same;
    if (primary) alike << StandardFont::qtFamilies(serif ? StandardFont::Kind::Serif : kind);
    for (const QString &family : std::as_const(alike)) {
        FPDF_FONT font = loadFont(doc, family, bold, italic);
        if (hasGlyph(font, codePoint)) return font;
    }
    FPDF_FONT font = loadFont(doc, fallbackFamily(codePoint, serif, bold, italic),
                              bold, italic);
    if (!hasGlyph(font, codePoint) && (bold || italic))
        font = loadFont(doc, fallbackFamily(codePoint, serif, false, false), false, false);
    return hasGlyph(font, codePoint) ? font : nullptr;
}

PdfiumFonts::Face PdfiumFonts::faceOf(FPDF_FONT font)
{
    Face out;
    if (!font) return out;
    char name[128] = {};
    FPDFFont_GetBaseFontName(font, name, sizeof(name));
    const BaseName base = parseBaseName(QString::fromLatin1(name));
    out.family = systemFamily(base.family);
    out.bold   = drawsBold(font, base);
    int angle = 0;
    out.italic = base.italic || (FPDFFont_GetItalicAngle(font, &angle) && angle != 0);
    return out;
}

QString PdfiumFonts::familyName(FPDF_FONT font)
{
    // The family a font names itself, the same for every subset of it and
    // for a font loaded from the system, before saving and after.
    if (!font) return {};
    if (const QRawFont *raw = loadedFace(font)) return plainFamily(raw->familyName());
    const QString registered = registerWithQt(font);
    if (!registered.isEmpty()) return plainFamily(writableFamily(registered));
    char name[128] = {};
    FPDFFont_GetBaseFontName(font, name, sizeof(name));
    return resolvePdfFont(QString::fromLatin1(name)).family;
}

bool PdfiumFonts::hasFace(const QString &family, bool bold, bool italic)
{
    // Qt answers an unknown family or style with some other face.
    const QRawFont &raw = face(writableFamily(family), bold, italic);
    return raw.isValid()
        && plainFamily(raw.familyName()).compare(plainFamily(writableFamily(family)),
                                                 Qt::CaseInsensitive) == 0
        && (raw.weight() >= 600) == bold && (raw.style() != QFont::StyleNormal) == italic
        && !fontData(family, bold, italic).isEmpty();
}

QHash<QByteArray, QMap<quint32, uint>> PdfiumFonts::writtenGlyphs(FPDF_DOCUMENT doc)
{
    QHash<QByteArray, QMap<quint32, uint>> out;
    for (FPDF_FONT font : loadedFonts().value(doc)) {
        const auto written = writtenGlyphMap().constFind(font);
        if (!font || written == writtenGlyphMap().cend() || written->isEmpty()) continue;
        char name[256] = {};
        if (FPDFFont_GetBaseFontName(font, name, sizeof(name)) <= 1) continue;
        QMap<quint32, uint> &glyphs = out[QByteArray(name)];
        for (auto it = written->cbegin(); it != written->cend(); ++it)
            if (!glyphs.contains(it.key())) glyphs.insert(it.key(), it.value());
    }
    return out;
}

void PdfiumFonts::releaseFonts(FPDF_DOCUMENT doc)
{
    decodeCache().remove(doc);
    const QHash<QString, FPDF_FONT> fonts = loadedFonts().take(doc);
    for (FPDF_FONT font : fonts) {
        if (!font) continue;
        loadedFaces().remove(font);
        writtenGlyphMap().remove(font);
        FPDFFont_Close(font);
    }
}

QString PdfiumFonts::registerWithQt(FPDF_FONT font)
{
    if (!font || FPDFFont_GetIsEmbedded(font) != 1) return {};

    size_t size = 0;
    if (!FPDFFont_GetFontData(font, nullptr, 0, &size) || size == 0) return {};
    if (size > 32u * 1024 * 1024) return {};

    std::vector<uint8_t> buffer(size);
    size_t written = 0;
    if (!FPDFFont_GetFontData(font, buffer.data(), size, &written) || written == 0)
        return {};

    const QByteArray data(reinterpret_cast<const char *>(buffer.data()),
                          static_cast<qsizetype>(written));
    const QByteArray key = QCryptographicHash::hash(data, QCryptographicHash::Sha1);
    const auto known = registry().constFind(key);
    if (known != registry().constEnd()) return *known;

    // A program registered under a family Qt already knows would answer later
    // requests for that family, so it goes in under a name of its own.
    QByteArray registered = data;
    const QRawFont raw(data, 12);
    QSet<QString> existing;
    for (const QString &f : QFontDatabase::families()) existing.insert(plainFamily(f).toLower());
    const QString own = plainFamily(raw.familyName());
    if (raw.isValid() && existing.contains(own.toLower())) {
        QString unique = own + QStringLiteral(" (PDF)");
        for (int n = 2; existing.contains(unique.toLower()); ++n)
            unique = own + QStringLiteral(" (PDF %1)").arg(n);
        registered = renamedSfnt(data, unique, raw.styleName(),
                                 postScriptName(unique, raw.styleName()));
        if (registered.isEmpty()) {
            registry().insert(key, QString());
            return {};
        }
    }

    const int id = QFontDatabase::addApplicationFontFromData(registered);
    const QStringList families =
        id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
    QString family = families.isEmpty() ? QString() : families.first();
    // A program Qt reads but cannot rename, a CFF, which names its family
    // itself, would take over a family the system has; the system's own face
    // stands for it then.
    if (!family.isEmpty() && existing.contains(plainFamily(family).toLower())) {
        QFontDatabase::removeApplicationFont(id);
        family.clear();
    }
    registry().insert(key, family);
    if (!family.isEmpty() && registered != data) renamedFamilies().insert(family, own);
    return family;
}

#endif
