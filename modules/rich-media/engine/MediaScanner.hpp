// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include "rich-media/engine/MediaAsset.hpp"

#include <QList>
#include <QString>

/// Finds the media in a PDF.
///
/// qpdf rather than PDFium: the path to a video runs through nested
/// dictionaries (/RichMediaContent, /Assets, a name tree, a filespec), and
/// PDFium's public API only reports an annotation's subtype and rectangle.
namespace MediaScanner {

bool available();

/// Every medium in `pdfPath`. Empty when the file cannot be read.
QList<MediaAsset> scan(const QString &pdfPath);

} // namespace MediaScanner
