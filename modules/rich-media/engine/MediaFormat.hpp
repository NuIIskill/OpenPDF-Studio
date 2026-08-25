// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QSize>
#include <QString>

/// What is inside a media file, read from the file itself.
///
/// No external program: a PDF may embed anything, and the question "will this
/// play for the person who receives the document" must be answerable the same
/// way on every platform. Asking ffprobe made the answer depend on whether
/// somebody had installed it, which is precisely the kind of difference this
/// avoids.
///
/// The answer that matters is narrow: H.264 in an MP4 container. That is the
/// common denominator across PDF viewers, browsers and phones, and both halves
/// of it are visible in the first few kilobytes of the file.
namespace MediaFormat {

struct Info
{
    bool    readable { false };   ///< the file could be identified
    QString container;            ///< "mp4", "matroska", "avi", "asf", …
    QString videoCodec;           ///< "h264", "hevc", "av1", "vp9", "mpeg4", …
    QSize   size;                 ///< pixels, invalid when unknown
    double  durationSec { 0.0 };

    /// H.264 in an MP4 container.
    bool playsEverywhere() const;
    /// Video is already H.264, so repackaging is enough.
    bool videoIsH264() const;
};

Info inspect(const QString &path);

} // namespace MediaFormat
