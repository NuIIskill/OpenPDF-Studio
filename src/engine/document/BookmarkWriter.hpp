#pragma once

#include "engine/document/WriteGrant.hpp"
#include "engine/document/PdfBookmark.hpp"

#include <QString>

namespace BookmarkWriter {

bool available();

bool write(const WriteGrant &path, const QList<PdfBookmark> &bookmarks,
           const QString &password = {});

}
