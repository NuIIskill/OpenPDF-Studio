#include "engine/export/PdfAGlyphs.hpp"

#ifdef HAVE_QPDF

#include "engine/export/FontProgram.hpp"
#include "engine/export/GlyphNames.hpp"

#include <QCoreApplication>
#include <QRegularExpression>

#include <cmath>
#include <map>
#include <optional>
#include <vector>

namespace {

enum class Verdict { Fine, Missing, Widths, Setup, Unknown };

bool nameIs(QPDFObjectHandle object, const char *name)
{
    return object.isName() && object.getName() == name;
}

QByteArray streamData(QPDFObjectHandle stream)
{
    if (!stream.isStream()) return {};
    try {
        const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_all);
        return data ? QByteArray(reinterpret_cast<const char *>(data->getBuffer()), qsizetype(data->getSize()))
                    : QByteArray();
    } catch (const std::exception &) {
        return {};
    }
}

FontProgram programOf(QPDFObjectHandle descriptor)
{
    if (!descriptor.isDictionary()) return {};
    if (QPDFObjectHandle file = descriptor.getKey("/FontFile2"); file.isStream())
        return FontProgram::trueType(streamData(file));
    if (QPDFObjectHandle file = descriptor.getKey("/FontFile"); file.isStream()) {
        QPDFObjectHandle clear = file.getDict().getKey("/Length1");
        return FontProgram::type1(streamData(file), clear.isInteger() ? int(clear.getIntValue()) : 0);
    }
    if (QPDFObjectHandle file = descriptor.getKey("/FontFile3"); file.isStream())
        return nameIs(file.getDict().getKey("/Subtype"), "/OpenType") ? FontProgram::openType(streamData(file))
                                                                      : FontProgram::cff(streamData(file));
    return {};
}

std::map<int, QByteArray> differencesOf(QPDFObjectHandle encoding)
{
    std::map<int, QByteArray> out;
    QPDFObjectHandle diffs = encoding.isDictionary() ? encoding.getKey("/Differences")
                                                     : QPDFObjectHandle::newNull();
    if (!diffs.isArray()) return out;
    int code = -1;
    for (const QPDFObjectHandle &item : diffs.getArrayAsVector()) {
        if (item.isInteger()) code = int(item.getIntValue());
        else if (item.isName() && code >= 0 && code < 256)
            out[code++] = QByteArray::fromStdString(item.getName().substr(1));
    }
    return out;
}

std::optional<GlyphNames::Names> baseNames(const std::string &name)
{
    if (name == "/WinAnsiEncoding") return GlyphNames::winAnsi();
    if (name == "/MacRomanEncoding") return GlyphNames::macRoman();
    if (name == "/StandardEncoding") return GlyphNames::standard();
    return std::nullopt;
}

// The glyph names a simple font's codes stand for, as PDF readers resolve them; none when unknown.
std::optional<GlyphNames::Names> simpleNames(QPDFObjectHandle font, const GlyphNames::Names &builtin,
                                             bool symbolic)
{
    QPDFObjectHandle encoding = font.getKey("/Encoding");
    std::optional<GlyphNames::Names> names = builtin;
    if (encoding.isName()) {
        names = baseNames(encoding.getName());
    } else if (encoding.isDictionary()) {
        QPDFObjectHandle base = encoding.getKey("/BaseEncoding");
        if (base.isName()) names = baseNames(base.getName());
        else if (!symbolic) names = GlyphNames::standard();
    }
    if (!names) return names;
    for (const auto &[code, name] : differencesOf(encoding)) (*names)[size_t(code)] = name;
    return names;
}

double dictionaryWidth(QPDFObjectHandle font, int code)
{
    QPDFObjectHandle first = font.getKey("/FirstChar"), widths = font.getKey("/Widths");
    const int index = first.isInteger() ? code - int(first.getIntValue()) : -1;
    if (widths.isArray() && index >= 0 && index < widths.getArrayNItems()
            && widths.getArrayItem(index).isNumber())
        return widths.getArrayItem(index).getNumericValue();
    QPDFObjectHandle descriptor = font.getKey("/FontDescriptor");
    QPDFObjectHandle missing = descriptor.isDictionary() ? descriptor.getKey("/MissingWidth")
                                                         : QPDFObjectHandle::newNull();
    return missing.isNumber() ? missing.getNumericValue() : 0;
}

bool widthsDiffer(double dictionary, double program)
{
    return !std::isnan(program) && std::abs(dictionary - program) > 1.0;
}

// The glyph a TrueType font's code selects; a code its cmap maps counts even when it maps to glyph 0.
int trueTypeGlyph(const FontProgram &program, const GlyphNames::Names *names, bool symbolic, int code)
{
    if (symbolic) {
        if (program.hasCmap(3, 0))
            for (const quint32 offset : { 0u, 0xF000u, 0xF100u, 0xF200u })
                if (const int g = program.glyphFromCmap(3, 0, offset + quint32(code)); g >= 0) return g;
        return program.glyphFromCmap(1, 0, quint32(code));
    }
    const QByteArray name = names ? (*names)[size_t(code)] : QByteArray();
    if (name.isEmpty() || name == ".notdef") return -1;
    if (const char32_t unicode = GlyphNames::unicodeOf(name); unicode && program.hasCmap(3, 1))
        if (const int g = program.glyphFromCmap(3, 1, unicode); g >= 0) return g;
    if (program.hasCmap(1, 0)) {
        const GlyphNames::Names mac = GlyphNames::macRoman();
        for (int c = 0; c < 256; ++c)
            if (mac[size_t(c)] == name)
                if (const int g = program.glyphFromCmap(1, 0, quint32(c)); g >= 0) return g;
    }
    return program.glyphByName.value(name, -1);
}

// What PDF/A says about how a TrueType font maps its codes (ISO 19005-2, 6.2.11.6).
bool trueTypeSetupFits(QPDFObjectHandle font, const FontProgram &program, bool symbolic)
{
    QPDFObjectHandle encoding = font.getKey("/Encoding");
    if (symbolic) return encoding.isNull() && (program.hasCmap(3, 0) || program.cmapCount() == 1);
    QPDFObjectHandle base = encoding.isDictionary() ? encoding.getKey("/BaseEncoding") : encoding;
    if (!nameIs(base, "/WinAnsiEncoding") && !nameIs(base, "/MacRomanEncoding")) return false;
    const std::map<int, QByteArray> diffs = differencesOf(encoding);
    if (diffs.empty()) return true;
    if (!program.hasCmap(3, 1)) return false;
    for (const auto &entry : diffs)
        if (!GlyphNames::unicodeOf(entry.second)) return false;
    return true;
}

Verdict checkSimple(QPDFObjectHandle font, const std::set<int> &codes)
{
    QPDFObjectHandle descriptor = font.getKey("/FontDescriptor");
    const FontProgram program = programOf(descriptor);
    if (!program.valid()) return Verdict::Unknown;
    QPDFObjectHandle flags = descriptor.getKey("/Flags");
    const bool symbolic = flags.isInteger() && (flags.getIntValue() & 4);
    const bool trueType = program.kind == FontProgram::Kind::TrueType;
    if (trueType && nameIs(font.getKey("/Subtype"), "/TrueType") && !trueTypeSetupFits(font, program, symbolic))
        return Verdict::Setup;
    const std::optional<GlyphNames::Names> names = simpleNames(font, program.builtinEncoding, symbolic);
    if (!trueType && !names) return Verdict::Unknown;

    for (const int code : codes) {
        int glyph = -1;
        if (trueType) {
            glyph = trueTypeGlyph(program, names ? &*names : nullptr, symbolic, code);
        } else {
            const QByteArray name = (*names)[size_t(code)];
            if (!name.isEmpty() && name != ".notdef") glyph = program.glyphByName.value(name, -1);
        }
        if (glyph < 0 || glyph >= program.glyphCount) return Verdict::Missing;
        if (widthsDiffer(dictionaryWidth(font, code), program.width(glyph))) return Verdict::Widths;
    }
    return Verdict::Fine;
}

struct CMap {
    std::vector<std::pair<QByteArray, QByteArray>> spaces;
    std::map<quint32, int> chars;
    std::vector<std::tuple<quint32, quint32, int>> ranges;
    bool identity { false };
};

quint32 codeValue(const QByteArray &bytes)
{
    quint32 v = 0;
    for (const char c : bytes) v = (v << 8) | quint8(c);
    return v;
}

std::optional<CMap> cmapOf(QPDFObjectHandle encoding)
{
    CMap cmap;
    if (nameIs(encoding, "/Identity-H") || nameIs(encoding, "/Identity-V")) {
        cmap.identity = true;
        cmap.spaces.push_back({ QByteArray(2, '\0'), QByteArray(2, '\xFF') });
        return cmap;
    }
    if (!encoding.isStream()) return std::nullopt;
    const QString text = QString::fromLatin1(streamData(encoding));
    QPDFObjectHandle use = encoding.getDict().getKey("/UseCMap");
    if (nameIs(use, "/Identity-H") || nameIs(use, "/Identity-V")
            || text.contains(QLatin1String("/Identity-H usecmap"))
            || text.contains(QLatin1String("/Identity-V usecmap")))
        cmap.identity = true;
    else if (!use.isNull() || text.contains(QLatin1String("usecmap")))
        return std::nullopt;
    const auto hex = [](const QString &s) { return QByteArray::fromHex(s.toLatin1()); };
    static const QRegularExpression section(QStringLiteral("begin(codespacerange|cidrange|cidchar)(.*?)end\\1"),
                                            QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression range(QStringLiteral("<([0-9A-Fa-f]+)>\\s*<([0-9A-Fa-f]+)>(?:\\s*(\\d+))?"));
    static const QRegularExpression single(QStringLiteral("<([0-9A-Fa-f]+)>\\s*(\\d+)"));
    for (auto it = section.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString kind = m.captured(1), body = m.captured(2);
        if (kind == QLatin1String("cidchar")) {
            for (auto e = single.globalMatch(body); e.hasNext();) {
                const QRegularExpressionMatch c = e.next();
                cmap.chars[codeValue(hex(c.captured(1)))] = c.captured(2).toInt();
            }
            continue;
        }
        for (auto e = range.globalMatch(body); e.hasNext();) {
            const QRegularExpressionMatch r = e.next();
            const QByteArray low = hex(r.captured(1)), high = hex(r.captured(2));
            if (kind == QLatin1String("codespacerange")) cmap.spaces.push_back({ low, high });
            else cmap.ranges.push_back({ codeValue(low), codeValue(high), r.captured(3).toInt() });
        }
    }
    if (cmap.spaces.empty()) return std::nullopt;
    return cmap;
}

std::vector<int> cidsOf(const CMap &cmap, const std::string &text)
{
    std::vector<int> cids;
    const QByteArray bytes = QByteArray::fromStdString(text);
    for (qsizetype i = 0; i < bytes.size();) {
        qsizetype length = 0;
        for (int n = 1; n <= 4 && !length && i + n <= bytes.size(); ++n)
            for (const auto &[low, high] : cmap.spaces) {
                if (low.size() != n) continue;
                bool inside = true;
                for (int b = 0; b < n && inside; ++b)
                    inside = quint8(bytes.at(i + b)) >= quint8(low.at(b))
                          && quint8(bytes.at(i + b)) <= quint8(high.at(b));
                if (inside) { length = n; break; }
            }
        if (!length) length = 1;
        const quint32 code = codeValue(bytes.mid(i, length));
        int cid = cmap.identity ? int(code) : 0;
        if (const auto c = cmap.chars.find(code); c != cmap.chars.end()) cid = c->second;
        else
            for (const auto &[low, high, start] : cmap.ranges)
                if (code >= low && code <= high) { cid = start + int(code - low); break; }
        cids.push_back(cid);
        i += length;
    }
    return cids;
}

double cidWidth(QPDFObjectHandle cidFont, int cid)
{
    QPDFObjectHandle w = cidFont.getKey("/W");
    if (w.isArray()) {
        const std::vector<QPDFObjectHandle> items = w.getArrayAsVector();
        for (size_t i = 0; i + 1 < items.size();) {
            if (!items[i].isInteger()) break;
            const int first = int(items[i].getIntValue());
            if (items[i + 1].isArray()) {
                const int n = items[i + 1].getArrayNItems();
                if (cid >= first && cid < first + n && items[i + 1].getArrayItem(cid - first).isNumber())
                    return items[i + 1].getArrayItem(cid - first).getNumericValue();
                i += 2;
            } else if (i + 2 < items.size()) {
                if (cid >= first && cid <= int(items[i + 1].getNumericValue()) && items[i + 2].isNumber())
                    return items[i + 2].getNumericValue();
                i += 3;
            } else {
                break;
            }
        }
    }
    QPDFObjectHandle dw = cidFont.getKey("/DW");
    return dw.isNumber() ? dw.getNumericValue() : 1000;
}

Verdict checkComposite(QPDFObjectHandle font, const std::set<std::string> &strings)
{
    QPDFObjectHandle descendants = font.getKey("/DescendantFonts");
    if (!descendants.isArray() || descendants.getArrayNItems() == 0) return Verdict::Unknown;
    QPDFObjectHandle cidFont = descendants.getArrayItem(0);
    const FontProgram program = programOf(cidFont.getKey("/FontDescriptor"));
    const std::optional<CMap> cmap = cmapOf(font.getKey("/Encoding"));
    if (!program.valid() || !cmap) return Verdict::Unknown;
    const bool type2 = nameIs(cidFont.getKey("/Subtype"), "/CIDFontType2");
    QPDFObjectHandle map = cidFont.getKey("/CIDToGIDMap");
    const QByteArray gids = map.isStream() ? streamData(map) : QByteArray();
    if (type2 && !map.isStream() && !nameIs(map, "/Identity")) return Verdict::Unknown;

    for (const std::string &text : strings)
        for (const int cid : cidsOf(*cmap, text)) {
            int glyph = cid;
            if (type2 && map.isStream())
                glyph = 2 * qsizetype(cid) + 1 < gids.size()
                      ? (quint8(gids.at(2 * cid)) << 8) | quint8(gids.at(2 * cid + 1)) : 0;
            else if (!type2 && program.cidKeyed)
                glyph = program.glyphByCid.value(cid, 0);
            if (cid <= 0 || glyph <= 0 || glyph >= program.glyphCount) return Verdict::Missing;
            if (widthsDiffer(cidWidth(cidFont, cid), program.width(glyph))) return Verdict::Widths;
        }
    return Verdict::Fine;
}

// A Type 3 code without a glyph draws nothing; an empty glyph with the same width keeps it so.
bool repairType3(QPDF &pdf, QPDFObjectHandle font, const std::set<int> &codes, PdfA::Report &report)
{
    QPDFObjectHandle procs = font.getKey("/CharProcs");
    QPDFObjectHandle encoding = font.getKey("/Encoding");
    QPDFObjectHandle first = font.getKey("/FirstChar"), widths = font.getKey("/Widths");
    if (!procs.isDictionary() || !encoding.isDictionary() || !first.isInteger() || !widths.isArray())
        return false;
    std::map<int, QByteArray> names = differencesOf(encoding);
    bool changed = false;
    for (const int code : codes) {
        const QByteArray name = names.count(code) ? names[code] : QByteArray();
        if (!name.isEmpty() && name != ".notdef" && procs.getKey("/" + name.toStdString()).isStream()) continue;
        const double width = dictionaryWidth(font, code);
        const std::string empty = "/OPDFEmpty" + std::to_string(code);
        procs.replaceKey(empty, pdf.newStream(QByteArray::number(width, 'f', 3).toStdString() + " 0 d0"));
        names[code] = QByteArray::fromStdString(empty.substr(1));
        changed = true;
    }
    if (!changed) return true;

    int low = int(first.getIntValue()), high = low + widths.getArrayNItems() - 1;
    for (const int code : codes) { low = qMin(low, code); high = qMax(high, code); }
    QPDFObjectHandle list = QPDFObjectHandle::newArray();
    for (int c = low; c <= high; ++c) list.appendItem(QPDFObjectHandle::newReal(dictionaryWidth(font, c), 3));
    QPDFObjectHandle diffs = QPDFObjectHandle::newArray();
    int last = -2;
    for (const auto &[code, name] : names) {
        if (code != last + 1) diffs.appendItem(QPDFObjectHandle::newInteger(code));
        diffs.appendItem(QPDFObjectHandle::newName("/" + name.toStdString()));
        last = code;
    }
    font.replaceKey("/FirstChar", QPDFObjectHandle::newInteger(low));
    font.replaceKey("/LastChar", QPDFObjectHandle::newInteger(high));
    font.replaceKey("/Widths", list);
    encoding.replaceKey("/Differences", diffs);
    ++report.repairs;
    return true;
}

QString shownName(QPDFObjectHandle font)
{
    QPDFObjectHandle base = font.getKey("/BaseFont");
    QString name = base.isName() ? QString::fromStdString(base.getName().substr(1)) : QString();
    if (name.size() > 7 && name.at(6) == u'+') name = name.mid(7);
    return name.isEmpty() ? QCoreApplication::translate("PdfA", "(unnamed)") : name;
}

}

void PdfAGlyphs::check(QPDF &pdf, const PdfAContent::Use &use, PdfA::Report &report)
{
    for (const auto &entry : use.fonts) {
        const PdfAContent::FontUse &used = entry.second;
        QPDFObjectHandle font = used.font;
        if (!used.visible || !font.isDictionary()) continue;
        const QPDFObjectHandle subtype = font.getKey("/Subtype");
        Verdict verdict = Verdict::Fine;
        if (nameIs(subtype, "/Type3")) {
            if (!repairType3(pdf, font, used.codes, report)) verdict = Verdict::Unknown;
        } else if (nameIs(subtype, "/Type0")) {
            verdict = checkComposite(font, used.strings);
        } else {
            QPDFObjectHandle descriptor = font.getKey("/FontDescriptor");
            const bool embedded = descriptor.isDictionary() && (descriptor.hasKey("/FontFile")
                                  || descriptor.hasKey("/FontFile2") || descriptor.hasKey("/FontFile3"));
            if (embedded) verdict = checkSimple(font, used.codes);
        }
        const QString name = shownName(font);
        QString blocker;
        switch (verdict) {
        case Verdict::Fine:
            break;
        case Verdict::Missing:
            blocker = QCoreApplication::translate("PdfA", "The embedded font \"%1\" lacks characters "
                                                          "the document shows.").arg(name);
            break;
        case Verdict::Widths:
            blocker = QCoreApplication::translate("PdfA", "The character widths of the font \"%1\" "
                                                          "do not match its embedded program.").arg(name);
            break;
        case Verdict::Setup:
            blocker = QCoreApplication::translate("PdfA", "The font \"%1\" maps its characters in a "
                                                          "way PDF/A does not allow.").arg(name);
            break;
        case Verdict::Unknown:
            blocker = QCoreApplication::translate("PdfA", "The font \"%1\" cannot be checked for "
                                                          "PDF/A.").arg(name);
            break;
        }
        if (!blocker.isEmpty() && !report.blockers.contains(blocker)) report.blockers << blocker;
    }
}

#endif
