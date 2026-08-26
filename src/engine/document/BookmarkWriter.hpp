#pragma once

#include "engine/document/PdfBookmark.hpp"

#include <QString>

namespace BookmarkWriter {

bool available();

/// Replaces the outline tree in `path` without touching page content.
bool write(const QString &path, const QList<PdfBookmark> &bookmarks,
           const QString &password = {});

} // namespace BookmarkWriter
