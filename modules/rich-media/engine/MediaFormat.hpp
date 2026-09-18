// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QSize>
#include <QString>

/// What is inside a media file, read from the file itself.
namespace MediaFormat {

struct Info
{
    bool    readable { false };
    QString container;
    QString videoCodec;
    QSize   size;
    double  durationSec { 0.0 };

    bool playsEverywhere() const;

    bool videoIsH264() const;
};

Info inspect(const QString &path);

}
