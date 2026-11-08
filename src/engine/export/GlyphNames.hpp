#pragma once

#include <QByteArray>

#include <array>

/// Glyph names of the standard PDF encodings and the characters they stand for.
namespace GlyphNames {

using Names = std::array<QByteArray, 256>;

Names winAnsi();
Names standard();
Names macRoman();

char32_t unicodeOf(const QByteArray &name);
char32_t macRomanUnicode(int code);

}
