#include "engine/export/GlyphNames.hpp"

#include <QChar>
#include <QHash>
#include <QList>
#include <QString>

#ifdef HAVE_QPDF
#  include <qpdf/QUtil.hh>
#endif

namespace {

struct Glyph {
    int         code;
    const char *name;
    char32_t    unicode;
};

// WinAnsiEncoding from 0x20 to 0x7E; letters are named by themselves.
constexpr const char *kAscii[] = {
    "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand", "quotesingle",
    "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period", "slash", "zero",
    "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "colon", "semicolon",
    "less", "equal", "greater", "question", "at", nullptr, "bracketleft", "backslash",
    "bracketright", "asciicircum", "underscore", "grave", nullptr, "braceleft", "bar",
    "braceright", "asciitilde"
};

constexpr Glyph kWinAnsi[] = {
    { 0x80, "Euro", 0x20AC }, { 0x82, "quotesinglbase", 0x201A }, { 0x83, "florin", 0x0192 },
    { 0x84, "quotedblbase", 0x201E }, { 0x85, "ellipsis", 0x2026 }, { 0x86, "dagger", 0x2020 },
    { 0x87, "daggerdbl", 0x2021 }, { 0x88, "circumflex", 0x02C6 }, { 0x89, "perthousand", 0x2030 },
    { 0x8A, "Scaron", 0x0160 }, { 0x8B, "guilsinglleft", 0x2039 }, { 0x8C, "OE", 0x0152 },
    { 0x8E, "Zcaron", 0x017D }, { 0x91, "quoteleft", 0x2018 }, { 0x92, "quoteright", 0x2019 },
    { 0x93, "quotedblleft", 0x201C }, { 0x94, "quotedblright", 0x201D }, { 0x95, "bullet", 0x2022 },
    { 0x96, "endash", 0x2013 }, { 0x97, "emdash", 0x2014 }, { 0x98, "tilde", 0x02DC },
    { 0x99, "trademark", 0x2122 }, { 0x9A, "scaron", 0x0161 }, { 0x9B, "guilsinglright", 0x203A },
    { 0x9C, "oe", 0x0153 }, { 0x9E, "zcaron", 0x017E }, { 0x9F, "Ydieresis", 0x0178 },
    { 0xA0, "space", 0x0020 }, { 0xA1, "exclamdown", 0xA1 }, { 0xA2, "cent", 0xA2 },
    { 0xA3, "sterling", 0xA3 }, { 0xA4, "currency", 0xA4 }, { 0xA5, "yen", 0xA5 },
    { 0xA6, "brokenbar", 0xA6 }, { 0xA7, "section", 0xA7 }, { 0xA8, "dieresis", 0xA8 },
    { 0xA9, "copyright", 0xA9 }, { 0xAA, "ordfeminine", 0xAA }, { 0xAB, "guillemotleft", 0xAB },
    { 0xAC, "logicalnot", 0xAC }, { 0xAD, "hyphen", 0x002D }, { 0xAE, "registered", 0xAE },
    { 0xAF, "macron", 0xAF }, { 0xB0, "degree", 0xB0 }, { 0xB1, "plusminus", 0xB1 },
    { 0xB2, "twosuperior", 0xB2 }, { 0xB3, "threesuperior", 0xB3 }, { 0xB4, "acute", 0xB4 },
    { 0xB5, "mu", 0xB5 }, { 0xB6, "paragraph", 0xB6 }, { 0xB7, "periodcentered", 0xB7 },
    { 0xB8, "cedilla", 0xB8 }, { 0xB9, "onesuperior", 0xB9 }, { 0xBA, "ordmasculine", 0xBA },
    { 0xBB, "guillemotright", 0xBB }, { 0xBC, "onequarter", 0xBC }, { 0xBD, "onehalf", 0xBD },
    { 0xBE, "threequarters", 0xBE }, { 0xBF, "questiondown", 0xBF }, { 0xC0, "Agrave", 0xC0 },
    { 0xC1, "Aacute", 0xC1 }, { 0xC2, "Acircumflex", 0xC2 }, { 0xC3, "Atilde", 0xC3 },
    { 0xC4, "Adieresis", 0xC4 }, { 0xC5, "Aring", 0xC5 }, { 0xC6, "AE", 0xC6 },
    { 0xC7, "Ccedilla", 0xC7 }, { 0xC8, "Egrave", 0xC8 }, { 0xC9, "Eacute", 0xC9 },
    { 0xCA, "Ecircumflex", 0xCA }, { 0xCB, "Edieresis", 0xCB }, { 0xCC, "Igrave", 0xCC },
    { 0xCD, "Iacute", 0xCD }, { 0xCE, "Icircumflex", 0xCE }, { 0xCF, "Idieresis", 0xCF },
    { 0xD0, "Eth", 0xD0 }, { 0xD1, "Ntilde", 0xD1 }, { 0xD2, "Ograve", 0xD2 },
    { 0xD3, "Oacute", 0xD3 }, { 0xD4, "Ocircumflex", 0xD4 }, { 0xD5, "Otilde", 0xD5 },
    { 0xD6, "Odieresis", 0xD6 }, { 0xD7, "multiply", 0xD7 }, { 0xD8, "Oslash", 0xD8 },
    { 0xD9, "Ugrave", 0xD9 }, { 0xDA, "Uacute", 0xDA }, { 0xDB, "Ucircumflex", 0xDB },
    { 0xDC, "Udieresis", 0xDC }, { 0xDD, "Yacute", 0xDD }, { 0xDE, "Thorn", 0xDE },
    { 0xDF, "germandbls", 0xDF }, { 0xE0, "agrave", 0xE0 }, { 0xE1, "aacute", 0xE1 },
    { 0xE2, "acircumflex", 0xE2 }, { 0xE3, "atilde", 0xE3 }, { 0xE4, "adieresis", 0xE4 },
    { 0xE5, "aring", 0xE5 }, { 0xE6, "ae", 0xE6 }, { 0xE7, "ccedilla", 0xE7 },
    { 0xE8, "egrave", 0xE8 }, { 0xE9, "eacute", 0xE9 }, { 0xEA, "ecircumflex", 0xEA },
    { 0xEB, "edieresis", 0xEB }, { 0xEC, "igrave", 0xEC }, { 0xED, "iacute", 0xED },
    { 0xEE, "icircumflex", 0xEE }, { 0xEF, "idieresis", 0xEF }, { 0xF0, "eth", 0xF0 },
    { 0xF1, "ntilde", 0xF1 }, { 0xF2, "ograve", 0xF2 }, { 0xF3, "oacute", 0xF3 },
    { 0xF4, "ocircumflex", 0xF4 }, { 0xF5, "otilde", 0xF5 }, { 0xF6, "odieresis", 0xF6 },
    { 0xF7, "divide", 0xF7 }, { 0xF8, "oslash", 0xF8 }, { 0xF9, "ugrave", 0xF9 },
    { 0xFA, "uacute", 0xFA }, { 0xFB, "ucircumflex", 0xFB }, { 0xFC, "udieresis", 0xFC },
    { 0xFD, "yacute", 0xFD }, { 0xFE, "thorn", 0xFE }, { 0xFF, "ydieresis", 0xFF },
};

// StandardEncoding where it differs from WinAnsiEncoding below 0x80, and above.
constexpr Glyph kStandard[] = {
    { 0x27, "quoteright", 0x2019 }, { 0x60, "quoteleft", 0x2018 },
    { 0xA1, "exclamdown", 0xA1 }, { 0xA2, "cent", 0xA2 }, { 0xA3, "sterling", 0xA3 },
    { 0xA4, "fraction", 0x2044 }, { 0xA5, "yen", 0xA5 }, { 0xA6, "florin", 0x0192 },
    { 0xA7, "section", 0xA7 }, { 0xA8, "currency", 0xA4 }, { 0xA9, "quotesingle", 0x27 },
    { 0xAA, "quotedblleft", 0x201C }, { 0xAB, "guillemotleft", 0xAB },
    { 0xAC, "guilsinglleft", 0x2039 }, { 0xAD, "guilsinglright", 0x203A }, { 0xAE, "fi", 0xFB01 },
    { 0xAF, "fl", 0xFB02 }, { 0xB1, "endash", 0x2013 }, { 0xB2, "dagger", 0x2020 },
    { 0xB3, "daggerdbl", 0x2021 }, { 0xB4, "periodcentered", 0xB7 }, { 0xB6, "paragraph", 0xB6 },
    { 0xB7, "bullet", 0x2022 }, { 0xB8, "quotesinglbase", 0x201A }, { 0xB9, "quotedblbase", 0x201E },
    { 0xBA, "quotedblright", 0x201D }, { 0xBB, "guillemotright", 0xBB }, { 0xBC, "ellipsis", 0x2026 },
    { 0xBD, "perthousand", 0x2030 }, { 0xBF, "questiondown", 0xBF }, { 0xC1, "grave", 0x60 },
    { 0xC2, "acute", 0xB4 }, { 0xC3, "circumflex", 0x02C6 }, { 0xC4, "tilde", 0x02DC },
    { 0xC5, "macron", 0xAF }, { 0xC6, "breve", 0x02D8 }, { 0xC7, "dotaccent", 0x02D9 },
    { 0xC8, "dieresis", 0xA8 }, { 0xCA, "ring", 0x02DA }, { 0xCB, "cedilla", 0xB8 },
    { 0xCD, "hungarumlaut", 0x02DD }, { 0xCE, "ogonek", 0x02DB }, { 0xCF, "caron", 0x02C7 },
    { 0xD0, "emdash", 0x2014 }, { 0xE1, "AE", 0xC6 }, { 0xE3, "ordfeminine", 0xAA },
    { 0xE8, "Lslash", 0x0141 }, { 0xE9, "Oslash", 0xD8 }, { 0xEA, "OE", 0x0152 },
    { 0xEB, "ordmasculine", 0xBA }, { 0xF1, "ae", 0xE6 }, { 0xF5, "dotlessi", 0x0131 },
    { 0xF8, "lslash", 0x0142 }, { 0xF9, "oslash", 0xF8 }, { 0xFA, "oe", 0x0153 },
    { 0xFB, "germandbls", 0xDF },
};

// Names of accents, for letters named after their parts, as in "aogonek" or "Ccaron".
constexpr std::pair<char16_t, const char *> kAccents[] = {
    { 0x300, "grave" }, { 0x301, "acute" }, { 0x302, "circumflex" }, { 0x303, "tilde" },
    { 0x304, "macron" }, { 0x306, "breve" }, { 0x307, "dotaccent" }, { 0x308, "dieresis" },
    { 0x30A, "ring" }, { 0x30B, "hungarumlaut" }, { 0x30C, "caron" }, { 0x31B, "horn" },
    { 0x326, "commaaccent" }, { 0x327, "cedilla" }, { 0x328, "ogonek" },
};

constexpr std::pair<const char *, char32_t> kOthers[] = {
    { "Dcroat", 0x0110 }, { "dcroat", 0x0111 }, { "Hbar", 0x0126 }, { "hbar", 0x0127 },
    { "IJ", 0x0132 }, { "ij", 0x0133 }, { "kgreenlandic", 0x0138 }, { "Ldot", 0x013F },
    { "ldot", 0x0140 }, { "napostrophe", 0x0149 }, { "Eng", 0x014A }, { "eng", 0x014B },
    { "Tbar", 0x0166 }, { "tbar", 0x0167 }, { "longs", 0x017F }, { "nbspace", 0x00A0 },
    { "sfthyphen", 0x00AD }, { "Omega", 0x03A9 }, { "pi", 0x03C0 }, { "Delta", 0x2206 },
    { "notequal", 0x2260 }, { "infinity", 0x221E }, { "lessequal", 0x2264 },
    { "greaterequal", 0x2265 }, { "partialdiff", 0x2202 }, { "summation", 0x2211 },
    { "product", 0x220F }, { "integral", 0x222B }, { "radical", 0x221A }, { "approxequal", 0x2248 },
    { "lozenge", 0x25CA }, { "minus", 0x2212 }, { "apple", 0xF8FF },
};

QByteArray asciiName(int c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return QByteArray(1, char(c));
    const int index = c < 'A' ? c - 0x20 : c < 'a' ? c - 0x20 - 25 : c - 0x20 - 50;
    return kAscii[index];
}

struct Tables {
    QHash<QByteArray, char32_t> unicode;
    QHash<char32_t, QByteArray> name;

    void add(const QByteArray &glyph, char32_t value)
    {
        if (!unicode.contains(glyph)) unicode.insert(glyph, value);
        if (!name.contains(value)) name.insert(value, glyph);
    }
};

const Tables &tables()
{
    static const Tables t = [] {
        Tables t;
        for (int c = 0x20; c < 0x7F; ++c) t.add(asciiName(c), char32_t(c));
        for (const Glyph &g : kWinAnsi) t.add(g.name, g.unicode);
        for (const Glyph &g : kStandard) t.add(g.name, g.unicode);
        for (const auto &[glyph, value] : kOthers) t.add(glyph, value);
        for (char16_t c = 0xC0; c < 0x250; ++c) {
            const QString parts = QChar(c).decomposition();
            if (parts.size() != 2 || parts.at(0).unicode() >= 0x80 || !parts.at(0).isLetter()) continue;
            for (const auto &[mark, accent] : kAccents) {
                if (parts.at(1).unicode() != mark) continue;
                const QByteArray letter(1, char(parts.at(0).unicode()));
                t.add(letter + accent, c);
                // The glyph list names a cedilla under G, K, L, N, R and T a comma accent.
                if (mark == 0x327 && QByteArray("GKLNRTgklnrt").contains(letter.at(0)))
                    t.add(letter + "commaaccent", c);
            }
        }
        return t;
    }();
    return t;
}

}

GlyphNames::Names GlyphNames::winAnsi()
{
    Names names;
    for (int c = 0x20; c < 0x7F; ++c) names[size_t(c)] = asciiName(c);
    for (const Glyph &g : kWinAnsi) names[size_t(g.code)] = g.name;
    return names;
}

GlyphNames::Names GlyphNames::standard()
{
    Names names;
    for (int c = 0x20; c < 0x7F; ++c) names[size_t(c)] = asciiName(c);
    for (const Glyph &g : kStandard) names[size_t(g.code)] = g.name;
    return names;
}

GlyphNames::Names GlyphNames::macRoman()
{
    Names names;
    for (int c = 0x20; c < 0x7F; ++c) names[size_t(c)] = asciiName(c);
    for (int c = 0x80; c < 0x100; ++c) names[size_t(c)] = tables().name.value(macRomanUnicode(c));
    return names;
}

char32_t GlyphNames::unicodeOf(const QByteArray &name)
{
    const auto found = tables().unicode.constFind(name);
    if (found != tables().unicode.cend()) return *found;
    bool ok = false;
    if (name.size() == 7 && name.startsWith("uni")) {
        const uint value = name.mid(3).toUInt(&ok, 16);
        if (ok && value > 0x1F) return char32_t(value);
    } else if (name.size() >= 5 && name.size() <= 7 && name.startsWith('u')) {
        const uint value = name.mid(1).toUInt(&ok, 16);
        if (ok && value > 0x1F && value <= 0x10FFFF) return char32_t(value);
    }
    return 0;
}

char32_t GlyphNames::macRomanUnicode(int code)
{
#ifdef HAVE_QPDF
    const QString text = QString::fromStdString(QUtil::mac_roman_to_utf8(std::string(1, char(code))));
    const QList<uint> ucs = text.toUcs4();
    return ucs.isEmpty() ? 0 : char32_t(ucs.first());
#else
    return code < 0x80 ? char32_t(code) : 0;
#endif
}
