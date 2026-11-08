#include "engine/export/FontProgram.hpp"

#include <QRegularExpression>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace {

constexpr double kUnknown = std::numeric_limits<double>::quiet_NaN();

QByteArray decrypt(const QByteArray &cipher, quint16 key, int skip)
{
    QByteArray plain;
    plain.reserve(cipher.size());
    for (const char c : cipher) {
        const quint8 byte = quint8(c);
        plain.append(char(byte ^ (key >> 8)));
        key = quint16((byte + key) * 52845u + 22719u);
    }
    return skip >= 0 ? plain.mid(skip) : cipher;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\0'; }

bool isDelimiter(char c)
{
    return isSpace(c) || c == '/' || c == '[' || c == ']' || c == '{' || c == '}' || c == '(' || c == ')'
        || c == '<' || c == '>';
}

// The advance a Type 1 charstring sets with hsbw or sbw; unknown when a subroutine would set it.
double charstringWidth(const QByteArray &cs)
{
    std::vector<double> stack;
    for (qsizetype i = 0; i < cs.size();) {
        const int v = quint8(cs.at(i));
        if (v >= 32 && v <= 246) { stack.push_back(v - 139); i += 1; continue; }
        if (v >= 247 && v <= 254) {
            const int w = i + 1 < cs.size() ? quint8(cs.at(i + 1)) : 0;
            stack.push_back(v <= 250 ? (v - 247) * 256 + w + 108 : -(v - 251) * 256 - w - 108);
            i += 2;
            continue;
        }
        if (v == 255) {
            qint32 n = 0;
            for (int b = 1; b <= 4; ++b)
                n = qint32((quint32(n) << 8) | (i + b < cs.size() ? quint8(cs.at(i + b)) : 0));
            stack.push_back(n);
            i += 5;
            continue;
        }
        if (v == 13) return stack.size() >= 2 ? stack[1] : kUnknown;
        if (v == 12 && i + 1 < cs.size()) {
            const int op = quint8(cs.at(i + 1));
            if (op == 7) return stack.size() >= 3 ? stack[2] : kUnknown;
            if (op == 12 && stack.size() >= 2 && stack.back() != 0) {
                const double b = stack.back();
                stack.pop_back();
                stack.back() /= b;
                i += 2;
                continue;
            }
        }
        return kUnknown;
    }
    return kUnknown;
}

GlyphNames::Names encodingOf(const QByteArray &clear)
{
    if (clear.contains("/Encoding StandardEncoding")) return GlyphNames::standard();
    GlyphNames::Names names;
    const qsizetype start = clear.indexOf("/Encoding");
    if (start < 0) return names;
    static const QRegularExpression entry(QStringLiteral("dup\\s+(\\d+)\\s*/([^\\s/\\[\\]{}()<>]+)\\s+put"));
    auto it = entry.globalMatch(QString::fromLatin1(clear.mid(start)));
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const int code = m.captured(1).toInt();
        if (code >= 0 && code < 256) names[size_t(code)] = m.captured(2).toLatin1();
    }
    return names;
}

}

FontProgram FontProgram::type1(const QByteArray &data, int clearLength)
{
    FontProgram p;
    const qsizetype eexec = data.indexOf("eexec");
    if (eexec < 0) return p;
    const QByteArray clear = data.left(eexec);
    qsizetype start = clearLength > eexec && clearLength < data.size() ? clearLength : eexec + 5;
    if (start == eexec + 5)
        while (start < data.size() && isSpace(data.at(start))) ++start;
    QByteArray cipher = data.mid(start);
    if (cipher.size() >= 4 && std::all_of(cipher.cbegin(), cipher.cbegin() + 4,
                                          [](char c) { return std::isxdigit(quint8(c)); }))
        cipher = QByteArray::fromHex(cipher);
    const QByteArray plain = decrypt(cipher, 55665, 4);

    static const QRegularExpression lenIVPattern(QStringLiteral("/lenIV\\s+(-?\\d+)"));
    const QRegularExpressionMatch lenIVMatch = lenIVPattern.match(QString::fromLatin1(plain.left(4096)));
    const int lenIV = lenIVMatch.hasMatch() ? lenIVMatch.captured(1).toInt() : 4;
    static const QRegularExpression matrixPattern(QStringLiteral("/FontMatrix\\s*\\[\\s*([-+0-9.eE]+)"));
    const QRegularExpressionMatch matrix = matrixPattern.match(QString::fromLatin1(clear));
    const double scale = matrix.hasMatch() ? matrix.captured(1).toDouble() * 1000 : 1.0;

    qsizetype i = plain.indexOf("/CharStrings");
    if (i < 0 || (i = plain.indexOf("begin", i)) < 0) return p;
    i += 5;
    while (i < plain.size()) {
        while (i < plain.size() && isSpace(plain.at(i))) ++i;
        if (plain.mid(i, 3) == "end" && (i + 3 >= plain.size() || isDelimiter(plain.at(i + 3)))) break;
        if (i >= plain.size() || plain.at(i) != '/') {
            ++i;
            continue;
        }
        qsizetype j = ++i;
        while (j < plain.size() && !isDelimiter(plain.at(j))) ++j;
        const QByteArray name = plain.mid(i, j - i);
        while (j < plain.size() && isSpace(plain.at(j))) ++j;
        qsizetype k = j;
        while (k < plain.size() && std::isdigit(quint8(plain.at(k)))) ++k;
        const int length = plain.mid(j, k - j).toInt();
        while (k < plain.size() && isSpace(plain.at(k))) ++k;
        while (k < plain.size() && !isSpace(plain.at(k))) ++k;
        ++k;
        if (k <= j || length <= 0 || k + length > plain.size()) break;
        const double width = charstringWidth(decrypt(plain.mid(k, length), 4330, lenIV));
        p.glyphByName.insert(name, int(p.widths.size()));
        p.widths.push_back(std::isnan(width) ? width : width * scale);
        i = k + length;
    }
    if (p.widths.empty()) return p;
    p.kind = Kind::Type1;
    p.glyphCount = int(p.widths.size());
    p.builtinEncoding = encodingOf(clear);
    return p;
}
