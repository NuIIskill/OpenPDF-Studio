// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QString>

#include <functional>

/// Rewrites a media file as H.264 in MP4.
namespace MediaConvert {

bool available();

QString toolPath();

bool run(const QString &in, const QString &out,
         const std::function<bool(int)> &progress, int maxHeight = 0);

}
