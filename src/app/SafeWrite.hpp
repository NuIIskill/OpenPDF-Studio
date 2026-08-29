#pragma once

#include <QString>

namespace SafeWrite {

QString stagingPath(const QString &target);

bool commit(const QString &stagingPath, const QString &target);

void discard(const QString &stagingPath);

}
