#pragma once

#include <QString>
#include <QStringList>

/// Maps PDF base-14 fonts to matching Qt families.
namespace StandardFont {

enum class Kind { Sans, Serif, Mono };

Kind kindOf(const QString &family);

QStringList qtFamilies(Kind kind);

}
