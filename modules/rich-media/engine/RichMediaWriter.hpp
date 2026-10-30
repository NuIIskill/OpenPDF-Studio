// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include "engine/document/WriteGrant.hpp"

#include <QString>

class MediaSession;

/// Writes media into a finished PDF, as a pass over the staging file the backend already wrote (PageOverlay::writeTo).
namespace RichMediaWriter {

bool available();

bool apply(const WriteGrant &pdf, const MediaSession &session,
           const QString &password = QString());

bool writeEmpty(const WriteGrant &pdf);

}
