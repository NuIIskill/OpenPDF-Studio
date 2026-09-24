#pragma once

#include "engine/historymanager/DocumentHistory.hpp"

#include <QList>
#include <QString>

/// A change log written to disk, so it outlives a crash of the program.
namespace HistoryArchive {

struct Timeline {
    QList<DocumentHistory::Entry> entries;
    int                           current { -1 };
};

bool write(const QList<DocumentHistory::Entry> &entries, int current, const QString &path);

bool read(const QString &path, Timeline *timeline);

void discard(const QString &path);

}
