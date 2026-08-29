// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include "rich-media/engine/MediaAsset.hpp"

#include <QList>
#include <QString>

/// Finds the media in a PDF.
namespace MediaScanner {

bool available();

QList<MediaAsset> scan(const QString &pdfPath);

}
