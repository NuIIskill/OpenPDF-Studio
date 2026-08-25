// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QString>

/// One medium to insert: everything the panel asks for and the writer needs.
/// Counterpart to MediaAsset, which describes what is already in the document.
struct MediaSpec
{
    enum class Type { Video, Audio, WebEmbed, Button };

    Type    type { Type::Video };

    QString source;        ///< file to embed
    QString displayName;   ///< name it gets inside the PDF
    QString mimeType;
    QImage  poster;        ///< still for /AP; empty means drawn placeholder

    /// /RichMediaSettings /Activation: true = /PO (page open), false = /XA
    /// (click). Click is the default; nothing starts by itself.
    bool    activateOnPageOpen { false };

    bool    autoPlay     { false };
    bool    muted        { false };
    bool    loop         { false };
    bool    showControls { true };

    /// true = plays in a window of its own (/RichMediaSettings /Window).
    bool    floating { false };

    int     page { -1 };   ///< 0-based
    QRectF  bounds;        ///< PDF points, Y=0 at top

    /// true = gets a page of its own sized `pageSizePt`, inserted after
    /// `page` and filled edge to edge. `bounds` is then derived, not read.
    bool    ownPage { false };
    QSizeF  pageSizePt;

    bool isValid() const
    {
        if (source.isEmpty() || page < 0) return false;
        return ownPage ? !pageSizePt.isEmpty() : !bounds.isEmpty();
    }
};
