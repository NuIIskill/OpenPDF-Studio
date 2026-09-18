#pragma once

#include "engine/document/PdfBookmark.hpp"

#include <QString>

namespace BookmarkWriter {

bool available();

bool write(const QString &path, const QList<PdfBookmark> &bookmarks,
           const QString &password = {});

}
