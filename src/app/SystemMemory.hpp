#pragma once

#include <QtGlobal>

/// Reports the free memory of the machine, so caches and helpers can size themselves.
namespace SystemMemory {

qint64 availableMb();
qint64 cacheBytes();

}
