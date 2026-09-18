// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>

/// The still image that stands where the video sits.
namespace PosterFrame {

bool available();

QImage grab(const QString &videoPath, int maxWidth = 960);

QImage placeholder(const QSize &size, const QColor &tint = QColor());

}
