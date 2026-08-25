// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QRectF>
#include <QString>

/// One medium found in a PDF: where it sits and where its bytes are.
/// Deliberately without the bytes; MediaExtractor fetches those on demand.
struct MediaAsset
{
    enum class Kind { RichMedia, Screen, Movie };

    int     page   { -1 };      ///< 0-based
    QRectF  bounds;             ///< PDF points, Y=0 at top
    QString name;               ///< file name as written in the PDF
    QString mimeType;           ///< "video/mp4", empty when the PDF is silent
    Kind    kind   { Kind::RichMedia };

    /// Object number of the embedded file stream. 0 means the annotation
    /// points outside the document; those are never played.
    int     streamObject { 0 };
    int     streamGeneration { 0 };

    /// The annotation itself, needed to remove it.
    int     annotObject { 0 };
    int     annotGeneration { 0 };

    qint64  size { 0 };         ///< /Params /Size, 0 when absent

    bool isEmbedded() const { return streamObject > 0; }
    bool isValid()    const { return page >= 0 && !bounds.isEmpty(); }
};
