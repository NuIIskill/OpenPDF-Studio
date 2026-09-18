// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QString>

/// One medium to insert: everything the panel asks for and the writer needs.
struct MediaSpec
{
    enum class Type { Video, Audio, WebEmbed, Button };

    Type    type { Type::Video };

    QString source;
    QString displayName;
    QString mimeType;
    QImage  poster;

    bool    activateOnPageOpen { false };

    bool    autoPlay     { false };
    bool    muted        { false };
    bool    loop         { false };
    bool    showControls { true };

    bool    floating { false };

    int     page { -1 };
    QRectF  bounds;

    bool    ownPage { false };
    QSizeF  pageSizePt;

    bool isValid() const
    {
        if (source.isEmpty() || page < 0) return false;
        return ownPage ? !pageSizePt.isEmpty() : !bounds.isEmpty();
    }
};
