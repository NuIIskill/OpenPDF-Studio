// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>

/// The still image that stands where the video sits.
///
/// Viewers draw an annotation's appearance stream, so without a poster the
/// spot is a white hole in anything that cannot play the video.
///
/// The frame comes from Qt Multimedia, the same decoder that plays the video.
/// Wherever playback works a poster works, and nothing extra has to be
/// installed for it.
namespace PosterFrame {

/// Always true; kept so callers need not care where the frame comes from.
bool available();

/// A frame from the video, empty when ffmpeg is missing or fails.
QImage grab(const QString &videoPath, int maxWidth = 960);

/// Dark area with a play mark.
QImage placeholder(const QSize &size, const QColor &tint = QColor());

} // namespace PosterFrame
