#include "engine/document/PdfiumFonts.hpp"

#include "engine/edit/StandardFont.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "fpdf_edit.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QRawFont>

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

}

QByteArray PdfiumFonts::fontData(const QString &family, bool bold, bool italic)
{
    const QString schluessel = family + (bold ? QStringLiteral("|f") : QString())
                                      + (italic ? QStringLiteral("|k") : QString());
    static QHash<QString, QByteArray> speicher;
    const auto da = speicher.constFind(schluessel);
    if (da != speicher.cend()) return *da;

    QByteArray daten;
    QFont f(family);
    f.setBold(bold);
    f.setItalic(italic);
    f.setStyleStrategy(QFont::NoFontMerging);
    const QRawFont raw = QRawFont::fromFont(f);
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
