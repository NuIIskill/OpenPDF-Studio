#pragma once

#include "engine/document/WriteGrant.hpp"
#include <QHash>
#include <QList>
#include <QRectF>
#include <QString>

/// Writes a document with areas redacted, rebuilt from its pages and checked afterwards.
namespace RedactionWriter {

using Areas = QHash<int, QList<QRectF>>;

enum class Result { Written, Failed, Leaked };

bool available();

Result write(const QString &source, const WriteGrant &target, const Areas &areas,
             const QString &password);

}
