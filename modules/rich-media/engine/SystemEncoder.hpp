// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QString>

#include <functional>

/// Rewrites a media file as H.264 in MP4 with the encoders the operating system brings.
namespace SystemEncoder {

enum class Result { Done, Failed, Cancelled };

bool available();

QString name();

Result run(const QString &in, const QString &out,
           const std::function<bool(int)> &progress, int maxHeight = 0);

}
