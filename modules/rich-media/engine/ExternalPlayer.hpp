// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QString>

/// Hands a media file to a player outside the program.
namespace ExternalPlayer {

bool play(const QString &filePath, const QString &command = QString());

QString lastPlayerName();

}
