// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include "rich-media/engine/MediaAsset.hpp"

#include <QString>

/// Writes the bytes of a found medium to disk, on click rather than on open.
namespace MediaExtractor {

QString extract(const QString &pdfPath, const MediaAsset &asset);

void clearCache(const QString &pdfPath = QString());

}
