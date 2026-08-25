// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include "rich-media/engine/MediaAsset.hpp"

#include <QString>

/// Writes the bytes of a found medium to disk, on click rather than on open.
/// One cache folder per process run; the same asset twice yields the same file.
namespace MediaExtractor {

/// Path of the extracted file, empty on failure. `pdfPath` must be the file
/// `asset` came from.
QString extract(const QString &pdfPath, const MediaAsset &asset);

/// Drops what was extracted for `pdfPath`. Empty path drops everything.
void clearCache(const QString &pdfPath = QString());

} // namespace MediaExtractor
