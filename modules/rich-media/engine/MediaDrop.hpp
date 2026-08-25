// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QSizeF>
#include <QString>

/// A video dropped onto the document becomes a page of its own, sized to the
/// video at 150 dpi with the annotation filling it. No widgets here, so it is
/// testable without a display.
namespace MediaDrop {

/// Suffixes worth a closer look. MediaFormat decides, not the name.
bool isVideoFile(const QString &path);

/// Page size in points for a video of these pixel dimensions, held between
/// 288 and 420 pt on the longest side.
QSizeF pageSizeFor(int videoWidth, int videoHeight);

/// The name the medium gets in the document. `original` is what the user
/// picked, `actual` what is really embedded; they differ after a conversion,
/// and the temporary file's name must not end up in the document.
QString displayNameFor(const QString &original, const QString &actual);

/// Writes a working copy of `pdfPath` with `source` as its own page after
/// `afterPage` and returns its path. Empty on failure. A working copy because
/// an added page cannot be a session change, as with the page organizer.
QString addAsOwnPage(const QString &pdfPath, const QString &source, int afterPage,
                     const QString &displayName = QString());

} // namespace MediaDrop
