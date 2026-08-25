// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QImage>
#include <QString>

/// One frame out of a video file.
///
/// Taken with whatever decodes video on this platform, which is the same
/// choice the player makes: Qt Multimedia everywhere, DirectShow on Windows.
/// Going through the working decoder matters, or the poster of a file that
/// plays perfectly well would come out as a drawn placeholder.
namespace VideoStill {

QImage grab(const QString &filePath, int maxWidth);

} // namespace VideoStill
