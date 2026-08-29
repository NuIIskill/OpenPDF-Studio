// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QRectF>
#include <QString>

/// One medium found in a PDF: where it sits and where its bytes are.
struct MediaAsset
{
    enum class Kind { RichMedia, Screen, Movie };

    int     page   { -1 };
    QRectF  bounds;
    QString name;
    QString mimeType;
    Kind    kind   { Kind::RichMedia };

    bool activateOnPageOpen { false };
    bool muted        { false };
    bool loop         { false };
    bool showControls { true };
    bool floating     { false };

    int     streamObject { 0 };
    int     streamGeneration { 0 };

    int     annotObject { 0 };
    int     annotGeneration { 0 };

    qint64  size { 0 };

    bool isEmbedded() const { return streamObject > 0; }
    bool isValid()    const { return page >= 0 && !bounds.isEmpty(); }
};
