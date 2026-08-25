// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QString>

/// Hands a media file to a player outside the program. The path that works
/// where the built-in player was not compiled in or plays nothing, Wine
/// included.
namespace ExternalPlayer {

/// Runs `command` when set, otherwise vlc, mpv, mplayer, then the system
/// association. "%1" in `command` is replaced by the file path; without the
/// placeholder the path is appended. False when nothing could be started.
bool play(const QString &filePath, const QString &command = QString());

QString lastPlayerName();

} // namespace ExternalPlayer
