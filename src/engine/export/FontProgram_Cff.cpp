#include "engine/export/FontProgram.hpp"

#include <cmath>
#include <limits>

namespace {

// The strings every CFF font shares, numbered by their position (CFF specification, appendix A).
constexpr const char *kStandardStrings[391] = {
    ".notdef", "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand",
    "quoteright", "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period", "slash",
    "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "colon",
    "semicolon", "less", "equal", "greater", "question", "at", "A", "B", "C", "D", "E", "F", "G", "H",
    "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "bracketleft", "backslash", "bracketright", "asciicircum", "underscore", "quoteleft", "a", "b",
    "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v",
    "w", "x", "y", "z", "braceleft", "bar", "braceright", "asciitilde", "exclamdown", "cent",
    "sterling", "fraction", "yen", "florin", "section", "currency", "quotesingle", "quotedblleft",
    "guillemotleft", "guilsinglleft", "guilsinglright", "fi", "fl", "endash", "dagger", "daggerdbl",
    "periodcentered", "paragraph", "bullet", "quotesinglbase", "quotedblbase", "quotedblright",
    "guillemotright", "ellipsis", "perthousand", "questiondown", "grave", "acute", "circumflex",
    "tilde", "macron", "breve", "dotaccent", "dieresis", "ring", "cedilla", "hungarumlaut", "ogonek",
    "caron", "emdash", "AE", "ordfeminine", "Lslash", "Oslash", "OE", "ordmasculine", "ae", "dotlessi",
    "lslash", "oslash", "oe", "germandbls", "onesuperior", "logicalnot", "mu", "trademark", "Eth",
    "onehalf", "plusminus", "Thorn", "onequarter", "divide", "brokenbar", "degree", "thorn",
    "threequarters", "twosuperior", "registered", "minus", "eth", "multiply", "threesuperior",
    "copyright", "Aacute", "Acircumflex", "Adieresis", "Agrave", "Aring", "Atilde", "Ccedilla",
    "Eacute", "Ecircumflex", "Edieresis", "Egrave", "Iacute", "Icircumflex", "Idieresis", "Igrave",
    "Ntilde", "Oacute", "Ocircumflex", "Odieresis", "Ograve", "Otilde", "Scaron", "Uacute",
    "Ucircumflex", "Udieresis", "Ugrave", "Yacute", "Ydieresis", "Zcaron", "aacute", "acircumflex",
    "adieresis", "agrave", "aring", "atilde", "ccedilla", "eacute", "ecircumflex", "edieresis",
    "egrave", "iacute", "icircumflex", "idieresis", "igrave", "ntilde", "oacute", "ocircumflex",
    "odieresis", "ograve", "otilde", "scaron", "uacute", "ucircumflex", "udieresis", "ugrave",
    "yacute", "ydieresis", "zcaron", "exclamsmall", "Hungarumlautsmall", "dollaroldstyle",
    "dollarsuperior", "ampersandsmall", "Acutesmall", "parenleftsuperior", "parenrightsuperior",
    "twodotenleader", "onedotenleader", "zerooldstyle", "oneoldstyle", "twooldstyle", "threeoldstyle",
    "fouroldstyle", "fiveoldstyle", "sixoldstyle", "sevenoldstyle", "eightoldstyle", "nineoldstyle",
    "commasuperior", "threequartersemdash", "periodsuperior", "questionsmall", "asuperior",
    "bsuperior", "centsuperior", "dsuperior", "esuperior", "isuperior", "lsuperior", "msuperior",
    "nsuperior", "osuperior", "rsuperior", "ssuperior", "tsuperior", "ff", "ffi", "ffl",
    "parenleftinferior", "parenrightinferior", "Circumflexsmall", "hyphensuperior", "Gravesmall",
    "Asmall", "Bsmall", "Csmall", "Dsmall", "Esmall", "Fsmall", "Gsmall", "Hsmall", "Ismall", "Jsmall",
    "Ksmall", "Lsmall", "Msmall", "Nsmall", "Osmall", "Psmall", "Qsmall", "Rsmall", "Ssmall", "Tsmall",
    "Usmall", "Vsmall", "Wsmall", "Xsmall", "Ysmall", "Zsmall", "colonmonetary", "onefitted", "rupiah",
    "Tildesmall", "exclamdownsmall", "centoldstyle", "Lslashsmall", "Scaronsmall", "Zcaronsmall",
    "Dieresissmall", "Brevesmall", "Caronsmall", "Dotaccentsmall", "Macronsmall", "figuredash",
    "hypheninferior", "Ogoneksmall", "Ringsmall", "Cedillasmall", "questiondownsmall", "oneeighth",
    "threeeighths", "fiveeighths", "seveneighths", "onethird", "twothirds", "zerosuperior",
    "foursuperior", "fivesuperior", "sixsuperior", "sevensuperior", "eightsuperior", "ninesuperior",
    "zeroinferior", "oneinferior", "twoinferior", "threeinferior", "fourinferior", "fiveinferior",
    "sixinferior", "seveninferior", "eightinferior", "nineinferior", "centinferior", "dollarinferior",
    "periodinferior", "commainferior", "Agravesmall", "Aacutesmall", "Acircumflexsmall", "Atildesmall",
    "Adieresissmall", "Aringsmall", "AEsmall", "Ccedillasmall", "Egravesmall", "Eacutesmall",
    "Ecircumflexsmall", "Edieresissmall", "Igravesmall", "Iacutesmall", "Icircumflexsmall",
    "Idieresissmall", "Ethsmall", "Ntildesmall", "Ogravesmall", "Oacutesmall", "Ocircumflexsmall",
    "Otildesmall", "Odieresissmall", "OEsmall", "Oslashsmall", "Ugravesmall", "Uacutesmall",
    "Ucircumflexsmall", "Udieresissmall", "Yacutesmall", "Thornsmall", "Ydieresissmall", "001.000",
    "001.001", "001.002", "001.003", "Black", "Bold", "Book", "Light", "Medium", "Regular", "Roman",
    "Semibold"
};

constexpr double kUnknown = std::numeric_limits<double>::quiet_NaN();

quint8 byteAt(const QByteArray &d, qsizetype at) { return at >= 0 && at < d.size() ? quint8(d.at(at)) : 0; }

quint16 u16(const QByteArray &d, qsizetype at) { return quint16((byteAt(d, at) << 8) | byteAt(d, at + 1)); }

quint32 u32(const QByteArray &d, qsizetype at) { return (quint32(u16(d, at)) << 16) | u16(d, at + 2); }

struct Index {
    std::vector<QByteArray> items;
    qsizetype               end { -1 };
};

Index readIndex(const QByteArray &d, qsizetype at)
{
    Index index;
    if (at < 0 || at + 2 > d.size()) return index;
    const int count = u16(d, at);
    if (count == 0) {
        index.end = at + 2;
        return index;
    }
    const int size = byteAt(d, at + 2);
    const qsizetype offsets = at + 3, data = offsets + qsizetype(count + 1) * size - 1;
    if (size < 1 || size > 4 || data >= d.size()) return index;
    const auto offset = [&](int i) {
        quint32 v = 0;
        for (int b = 0; b < size; ++b) v = (v << 8) | byteAt(d, offsets + qsizetype(i) * size + b);
        return v;
    };
    for (int i = 0; i < count; ++i) {
        const quint32 from = offset(i), to = offset(i + 1);
        if (to < from || data + to > d.size()) return {};
        index.items.push_back(d.mid(data + from, to - from));
    }
    index.end = data + offset(count);
    return index;
}

// Operators as keys, two-byte ones as 1200 plus their second byte.
using Dict = QHash<int, std::vector<double>>;

Dict readDict(const QByteArray &d)
{
    Dict dict;
    std::vector<double> operands;
    for (qsizetype i = 0; i < d.size();) {
        const int b0 = byteAt(d, i);
        if (b0 <= 21) {
            const int op = b0 == 12 ? 1200 + byteAt(d, i + 1) : b0;
            i += b0 == 12 ? 2 : 1;
            dict.insert(op, operands);
            operands.clear();
        } else if (b0 == 28) {
            operands.push_back(qint16(u16(d, i + 1)));
            i += 3;
        } else if (b0 == 29) {
            operands.push_back(qint32(u32(d, i + 1)));
            i += 5;
        } else if (b0 == 30) {
            QByteArray text;
            bool done = false;
            for (++i; i < d.size() && !done; ++i)
                for (const int n : { byteAt(d, i) >> 4, byteAt(d, i) & 15 }) {
                    if (n <= 9) text += char('0' + n);
                    else if (n == 0xA) text += '.';
                    else if (n == 0xB) text += 'E';
                    else if (n == 0xC) text += "E-";
                    else if (n == 0xE) text += '-';
                    else if (n == 0xF) { done = true; break; }
                }
            operands.push_back(text.toDouble());
        } else if (b0 >= 32 && b0 <= 246) {
            operands.push_back(b0 - 139);
            ++i;
        } else if (b0 >= 247 && b0 <= 250) {
            operands.push_back((b0 - 247) * 256 + byteAt(d, i + 1) + 108);
            i += 2;
        } else if (b0 >= 251 && b0 <= 254) {
            operands.push_back(-(b0 - 251) * 256 - byteAt(d, i + 1) - 108);
            i += 2;
        } else {
            ++i;
        }
    }
    return dict;
}

double value(const Dict &dict, int op, double fallback, size_t index = 0)
{
    const auto found = dict.constFind(op);
    return found != dict.cend() && found->size() > index ? found->at(index) : fallback;
}

// The advance a Type 2 charstring opens with; unknown when a subroutine would give it.
double charstringWidth(const QByteArray &cs, double defaultWidth, double nominalWidth)
{
    int args = 0;
    double first = 0;
    for (qsizetype i = 0; i < cs.size();) {
        const int b0 = byteAt(cs, i);
        double v = 0;
        if (b0 >= 32 && b0 <= 246) { v = b0 - 139; i += 1; }
        else if (b0 >= 247 && b0 <= 250) { v = (b0 - 247) * 256 + byteAt(cs, i + 1) + 108; i += 2; }
        else if (b0 >= 251 && b0 <= 254) { v = -(b0 - 251) * 256 - byteAt(cs, i + 1) - 108; i += 2; }
        else if (b0 == 28) { v = qint16(u16(cs, i + 1)); i += 3; }
        else if (b0 == 255) { v = qint32(u32(cs, i + 1)) / 65536.0; i += 5; }
        else {
            switch (b0) {
            case 1: case 3: case 18: case 23: case 19: case 20:
                return (args % 2) ? first + nominalWidth : defaultWidth;
            case 21:
                return args > 2 ? first + nominalWidth : defaultWidth;
            case 4: case 22:
                return args > 1 ? first + nominalWidth : defaultWidth;
            case 14:
                return (args == 1 || args == 5) ? first + nominalWidth : defaultWidth;
            default:
                return kUnknown;
            }
        }
        if (args++ == 0) first = v;
    }
    return kUnknown;
}

// The id each glyph has in the charset: a string for a simple font, a CID for a CID-keyed one.
std::vector<int> charsetIds(const QByteArray &d, int offset, int glyphs, bool *ok)
{
    std::vector<int> ids(size_t(glyphs), 0);
    *ok = true;
    if (offset == 0) {
        for (int g = 0; g < glyphs; ++g) ids[size_t(g)] = g;
        return ids;
    }
    if (offset <= 2) {
        *ok = false;
        return ids;
    }
    const int format = byteAt(d, offset);
    qsizetype at = offset + 1;
    for (int g = 1; g < glyphs && at < d.size();) {
        if (format == 0) {
            ids[size_t(g++)] = u16(d, at);
            at += 2;
            continue;
        }
        const int first = u16(d, at);
        const int left = format == 1 ? byteAt(d, at + 2) : u16(d, at + 2);
        at += format == 1 ? 3 : 4;
        for (int k = 0; k <= left && g < glyphs; ++k) ids[size_t(g++)] = first + k;
    }
    return ids;
}

std::vector<int> fdSelect(const QByteArray &d, int offset, int glyphs)
{
    std::vector<int> fds(size_t(glyphs), 0);
    const int format = byteAt(d, offset);
    if (format == 0) {
        for (int g = 0; g < glyphs; ++g) fds[size_t(g)] = byteAt(d, offset + 1 + g);
    } else if (format == 3) {
        const int ranges = u16(d, offset + 1);
        for (int r = 0; r < ranges; ++r) {
            const qsizetype at = offset + 3 + 3 * qsizetype(r);
            const int from = u16(d, at), to = u16(d, at + 3), fd = byteAt(d, at + 2);
            for (int g = from; g < to && g < glyphs; ++g) fds[size_t(g)] = fd;
        }
    }
    return fds;
}

std::pair<double, double> privateWidths(const QByteArray &d, const Dict &dict)
{
    const int size = int(value(dict, 18, 0, 0)), offset = int(value(dict, 18, 0, 1));
    const Dict priv = readDict(d.mid(offset, size));
    return { value(priv, 20, 0), value(priv, 21, 0) };
}

}

FontProgram FontProgram::cff(const QByteArray &d)
{
    FontProgram p;
    if (d.size() < 4) return p;
    const Index names = readIndex(d, byteAt(d, 2));
    const Index tops = readIndex(d, names.end);
    const Index strings = readIndex(d, tops.end);
    if (strings.end < 0 || tops.items.empty()) return p;
    const Dict top = readDict(tops.items.front());
    const Index charstrings = readIndex(d, qsizetype(value(top, 17, -1)));
    if (charstrings.end < 0 || charstrings.items.empty()) return p;

    const int glyphs = int(charstrings.items.size());
    const bool cid = top.contains(1230);
    bool ok = false;
    const std::vector<int> ids = charsetIds(d, int(value(top, 15, 0)), glyphs, &ok);
    if (!ok) return p;
    const auto stringFor = [&](int sid) -> QByteArray {
        if (sid < 391) return kStandardStrings[sid];
        return size_t(sid - 391) < strings.items.size() ? strings.items[size_t(sid - 391)] : QByteArray();
    };
    p.kind = Kind::Cff;
    p.glyphCount = glyphs;
    p.cidKeyed = cid;
    for (int g = 0; g < glyphs; ++g) {
        if (cid) p.glyphByCid.insert(ids[size_t(g)], g);
        else p.glyphByName.insert(stringFor(ids[size_t(g)]), g);
    }

    const double scale = value(top, 1207, 0.001) * 1000;
    std::vector<std::pair<double, double>> privates;
    std::vector<int> fds(size_t(glyphs), 0);
    if (cid) {
        const Index fdArray = readIndex(d, qsizetype(value(top, 1236, -1)));
        for (const QByteArray &fd : fdArray.items) privates.push_back(privateWidths(d, readDict(fd)));
        fds = fdSelect(d, int(value(top, 1237, 0)), glyphs);
    } else {
        privates.push_back(privateWidths(d, top));
    }
    p.widths.assign(size_t(glyphs), kUnknown);
    for (int g = 0; g < glyphs; ++g) {
        const size_t fd = size_t(fds[size_t(g)]);
        if (fd >= privates.size()) continue;
        p.widths[size_t(g)] = charstringWidth(charstrings.items[size_t(g)], privates[fd].first,
                                              privates[fd].second) * scale;
    }

    const int encoding = int(value(top, 16, 0));
    if (cid || encoding == 1) return p;
    if (encoding == 0) {
        p.builtinEncoding = GlyphNames::standard();
        return p;
    }
    const int format = byteAt(d, encoding);
    qsizetype at = encoding + 1;
    int g = 1;
    if ((format & 0x7F) == 0) {
        const int count = byteAt(d, at++);
        for (int i = 0; i < count && g < glyphs; ++i, ++g)
            p.builtinEncoding[byteAt(d, at++)] = stringFor(ids[size_t(g)]);
    } else {
        const int ranges = byteAt(d, at++);
        for (int r = 0; r < ranges; ++r, at += 2)
            for (int k = 0, code = byteAt(d, at); k <= byteAt(d, at + 1) && g < glyphs && code + k < 256; ++k)
                p.builtinEncoding[size_t(code + k)] = stringFor(ids[size_t(g++)]);
    }
    if (format & 0x80) {
        const int supplements = byteAt(d, at++);
        for (int i = 0; i < supplements; ++i, at += 3)
            p.builtinEncoding[byteAt(d, at)] = stringFor(u16(d, at + 1));
    }
    return p;
}
