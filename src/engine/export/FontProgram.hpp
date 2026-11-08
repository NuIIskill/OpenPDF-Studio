#pragma once

#include "engine/export/GlyphNames.hpp"

#include <QByteArray>
#include <QHash>

#include <vector>

/// What an embedded TrueType, CFF or Type1 font program holds: its glyphs, their names, codes and widths.
struct FontProgram {
    enum class Kind { None, TrueType, Cff, Type1 };

    Kind                  kind { Kind::None };
    int                   glyphCount { 0 };
    std::vector<double>   widths;
    QHash<QByteArray, int> glyphByName;
    GlyphNames::Names     builtinEncoding;
    bool                  cidKeyed { false };
    QHash<int, int>       glyphByCid;
    QByteArray            cmap;

    bool valid() const { return kind != Kind::None && glyphCount > 0; }
    bool hasCmap(int platform, int encoding) const;
    int cmapCount() const;
    int glyphFromCmap(int platform, int encoding, quint32 code) const;
    double width(int glyph) const;

    static FontProgram trueType(const QByteArray &data);
    static FontProgram cff(const QByteArray &data);
    static FontProgram type1(const QByteArray &data, int clearLength);
    static FontProgram openType(const QByteArray &data);
};
