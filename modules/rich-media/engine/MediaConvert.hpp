// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QString>

#include <functional>

/// Rewrites a media file as H.264 in MP4.
///
/// The one place that still needs an outside program. Reading a file's format
/// and grabbing a still from it are done without help (MediaFormat,
/// PosterFrame); encoding video is not something this program carries its own
/// code for, so ffmpeg does it when it is there. That is the same everywhere:
/// with ffmpeg the offer appears, without it the user is told what the
/// consequence is.
namespace MediaConvert {

/// ffmpeg found. Searched on PATH, next to the application, and in the usual
/// install locations.
bool available();

/// Path of the ffmpeg that would be used, empty when there is none.
QString toolPath();

/// Writes `in` to `out` as H.264 in MP4. `progress` gets 0…100 and returns
/// false to cancel. An H.264 stream is repackaged, not re-encoded.
///
/// `maxHeight` above zero scales the picture down to at most that many lines,
/// which is how a 4K video is made playable on a system whose decoder refuses
/// it. Scaling always re-encodes, so the repackaging shortcut does not apply.
bool run(const QString &in, const QString &out,
         const std::function<bool(int)> &progress, int maxHeight = 0);

} // namespace MediaConvert
