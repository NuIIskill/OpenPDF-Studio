#include "DocxExporter.hpp"

#include <QByteArray>
#include <QFile>

// ── minimal ZIP writer (store, no compression) ────────────────────────────────

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

struct ZEntry {
    QByteArray name;
    QByteArray data;
    uint32_t   crc    { 0 };
    uint32_t   offset { 0 };
};

static void writeLocal(QByteArray &zip, ZEntry &e)
{
    e.crc    = crc32Compute(e.data);
    e.offset = static_cast<uint32_t>(zip.size());
    zip += "\x50\x4B\x03\x04";
    u16le(zip, 20); u16le(zip, 0); u16le(zip, 0);
    u16le(zip, 0);  u16le(zip, 0);
    u32le(zip, e.crc);
    u32le(zip, static_cast<uint32_t>(e.data.size()));
    u32le(zip, static_cast<uint32_t>(e.data.size()));
    u16le(zip, static_cast<uint16_t>(e.name.size()));
    u16le(zip, 0);
    zip += e.name;
    zip += e.data;
}

static void writeCentral(QByteArray &cd, const ZEntry &e)
{
    cd += "\x50\x4B\x01\x02";
    u16le(cd, 20); u16le(cd, 20); u16le(cd, 0); u16le(cd, 0);
    u16le(cd, 0);  u16le(cd, 0);
    u32le(cd, e.crc);
    u32le(cd, static_cast<uint32_t>(e.data.size()));
    u32le(cd, static_cast<uint32_t>(e.data.size()));
    u16le(cd, static_cast<uint16_t>(e.name.size()));
    u16le(cd, 0); u16le(cd, 0); u16le(cd, 0); u16le(cd, 0);
    u32le(cd, 0); u32le(cd, e.offset);
    cd += e.name;
}

// ── DOCX XML builders ─────────────────────────────────────────────────────────

static QString xmlEsc(const QString &s)
{
    QString r;
    r.reserve(s.size() + 8);
    for (QChar c : s) {
        const ushort u = c.unicode();
        if      (u == '&')  r += QLatin1String("&amp;");
        else if (u == '<')  r += QLatin1String("&lt;");
        else if (u == '>')  r += QLatin1String("&gt;");
        else if (u == '"')  r += QLatin1String("&quot;");
        else if (u < 32 && u != '\n' && u != '\r' && u != '\t') { /* skip */ }
        else                r += c;
    }
    return r;
}

static QByteArray buildDocument(const QList<QString> &pageTexts, const QString &title)
{
    QString x;
    x += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
    x += QStringLiteral("<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
    x += QStringLiteral("<w:body>");

    if (!title.isEmpty()) {
        x += QStringLiteral("<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr>"
                            "<w:r><w:t>");
        x += xmlEsc(title);
        x += QStringLiteral("</w:t></w:r></w:p>");
    }

    for (int pg = 0; pg < pageTexts.size(); ++pg) {
        if (pg > 0)
            x += QStringLiteral("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>");

        for (const QString &line : pageTexts[pg].split(QLatin1Char('\n'))) {
            x += QStringLiteral("<w:p><w:r><w:t xml:space=\"preserve\">");
            x += xmlEsc(line);
            x += QStringLiteral("</w:t></w:r></w:p>");
        }
    }

    x += QStringLiteral("<w:sectPr/></w:body></w:document>");
    return x.toUtf8();
}

// ── public API ────────────────────────────────────────────────────────────────

bool DocxExporter::exportToDocx(const QString &outputPath,
                                const QList<QString> &pageTexts,
                                const QString &title)
{
    static const char kCT[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\""
        " ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>";

    static const char kRels[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\""
        " Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\""
        " Target=\"word/document.xml\"/>"
        "</Relationships>";

    static const char kDocRels[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"/>";

    QList<ZEntry> entries;
    entries.append({"[Content_Types].xml",          QByteArray(kCT),      0, 0});
    entries.append({"_rels/.rels",                  QByteArray(kRels),    0, 0});
    entries.append({"word/_rels/document.xml.rels", QByteArray(kDocRels), 0, 0});
    entries.append({"word/document.xml",            buildDocument(pageTexts, title), 0, 0});

    QByteArray zip;
    for (auto &e : entries)
        writeLocal(zip, e);

    QByteArray cd;
    for (const auto &e : entries)
        writeCentral(cd, e);

    const uint32_t cdOff  = static_cast<uint32_t>(zip.size());
    const uint32_t cdSize = static_cast<uint32_t>(cd.size());
    zip += cd;

    zip += "\x50\x4B\x05\x06";
    u16le(zip, 0); u16le(zip, 0);
    u16le(zip, static_cast<uint16_t>(entries.size()));
    u16le(zip, static_cast<uint16_t>(entries.size()));
    u32le(zip, cdSize);
    u32le(zip, cdOff);
    u16le(zip, 0);

    QFile f(outputPath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(zip) == zip.size();
}
