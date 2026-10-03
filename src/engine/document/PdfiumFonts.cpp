#include "engine/document/PdfiumFonts.hpp"

#include "engine/edit/StandardFont.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "fpdf_edit.h"
#include "fpdf_text.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QPainterPath>
#include <QRawFont>
#include <QStringList>

#include <algorithm>
#include <utility>

#include <vector>

namespace {

QHash<QByteArray, QString> &registry()
{
    static QHash<QByteArray, QString> cache;
    return cache;
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

QByteArray buildSfnt(const QRawFont &raw)
{
    QList<QPair<QByteArray, QByteArray>> tabellen;
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

QByteArray PdfiumFonts::fontData(const QString &family, bool bold, bool italic)
{
    const QString schluessel = family + (bold ? QStringLiteral("|f") : QString())
                                      + (italic ? QStringLiteral("|k") : QString());
    static QHash<QString, QByteArray> speicher;
    const auto da = speicher.constFind(schluessel);
    if (da != speicher.cend()) return *da;

    QByteArray daten;
    const QRawFont &raw = face(family, bold, italic);
    if (raw.isValid()) {

        const QByteArray os2 = raw.fontTable("OS/2");
        const quint16 fsType = os2.size() >= 10
            ? quint16((quint8(os2.at(8)) << 8) | quint8(os2.at(9))) : 0;
        if ((fsType & 0x000F) != 0x0002) daten = buildSfnt(raw);
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

struct Face {
    QString family;
    bool    bold   { false };
    bool    italic { false };
};

QHash<FPDF_DOCUMENT, QHash<QString, FPDF_FONT>> &loadedFonts()
{
    static QHash<FPDF_DOCUMENT, QHash<QString, FPDF_FONT>> fonts;
    return fonts;
}

QHash<FPDF_FONT, Face> &loadedFaces()
{
    static QHash<FPDF_FONT, Face> faces;
    return faces;
}

const QRawFont *loadedFace(FPDF_FONT font)
{
    const auto it = loadedFaces().constFind(font);
    return it == loadedFaces().cend() ? nullptr
                                      : &face(it->family, it->bold, it->italic);
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
    if (QChar::requiresSurrogates(codePoint)) return false;

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
    if (QChar::requiresSurrogates(codePoint)) {
        const QRawFont *raw = loadedFace(font);
        if (!raw || raw->pixelSize() <= 0.0) return 0.0;
        double width = 0.0;
        const QList<quint32> ids = raw->glyphIndexesForString(QString::fromUcs4(&codePoint, 1));
        for (const QPointF &a : raw->advancesForGlyphIndexes(ids, QRawFont::UseDesignMetrics))
            width += a.x();
        return width * sizePt / raw->pixelSize();
    }
    float width = 0.f;
    return FPDFFont_GetGlyphWidth(font, codePoint, static_cast<float>(sizePt), &width)
        ? double(width) : 0.0;
}

void PdfiumFonts::setText(FPDF_PAGEOBJECT object, FPDF_FONT font, const QString &text)
{
    const QList<uint> points = text.toUcs4();
    const bool astral = std::any_of(points.cbegin(), points.cend(),
                                    [](uint cp) { return QChar::requiresSurrogates(cp); });
    const QRawFont *raw = astral ? loadedFace(font) : nullptr;
    if (raw) {
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
    if (font) loadedFaces().insert(font, { family, bold, italic });
    return font;
}

FPDF_FONT PdfiumFonts::fallbackFont(FPDF_DOCUMENT doc, FPDF_FONT primary,
                                    char32_t codePoint)
{
    constexpr int kSerifFlag = 1 << 1;
    const bool serif = primary && (FPDFFont_GetFlags(primary) & kSerifFlag);
    const bool bold  = primary && FPDFFont_GetWeight(primary) >= 600;
    int angle = 0;
    const bool italic = primary && FPDFFont_GetItalicAngle(primary, &angle) && angle != 0;
    FPDF_FONT font = loadFont(doc, fallbackFamily(codePoint, serif, bold, italic),
                              bold, italic);
    if (!hasGlyph(font, codePoint) && (bold || italic))
        font = loadFont(doc, fallbackFamily(codePoint, serif, false, false), false, false);
    return hasGlyph(font, codePoint) ? font : nullptr;
}

void PdfiumFonts::releaseFonts(FPDF_DOCUMENT doc)
{
    decodeCache().remove(doc);
    const QHash<QString, FPDF_FONT> fonts = loadedFonts().take(doc);
    for (FPDF_FONT font : fonts) {
        if (!font) continue;
        loadedFaces().remove(font);
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

    const int id = QFontDatabase::addApplicationFontFromData(data);
    const QStringList families =
        id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
    const QString family = families.isEmpty() ? QString() : families.first();
    registry().insert(key, family);
    return family;
}

#endif
