// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QImage>
#include <QString>

/// One frame out of a video file.
namespace VideoStill {

QImage grab(const QString &filePath, int maxWidth);

}
