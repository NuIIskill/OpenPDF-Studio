#pragma once

#include <QString>

/// The field flags of the PDF specification that filling in and designing care about.
namespace FormFlags {

constexpr int ReadOnly  = 1 << 0;
constexpr int Required  = 1 << 1;
constexpr int Multiline = 1 << 12;
constexpr int Password  = 1 << 13;
constexpr int Editable  = 1 << 18;

// Set by the kind of a field, not by the user.
constexpr int KindBits  = (1 << 14) | (1 << 15) | (1 << 16) | (1 << 17);

inline const QString Off = QStringLiteral("Off");

}
