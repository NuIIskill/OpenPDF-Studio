#pragma once

#include <QList>
#include <QString>

/// Reads page ranges like "1-3, 5" as typed in the export and print dialogs.
namespace PageRange {

QList<int> parse(const QString &text, int pageCount, bool *ok);

}
