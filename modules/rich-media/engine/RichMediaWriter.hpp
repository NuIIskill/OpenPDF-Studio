// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QString>

class MediaSession;

/// Writes media into a finished PDF, as a pass over the staging file the
/// backend already wrote (PageOverlay::writeTo).
///
/// qpdf and not PDFium: a RichMedia annotation is nested dictionaries, a name
/// tree and an embedded file stream, and PDFium's public API can only set
/// strings on an annotation.
///
/// The shape written is the one Acrobat writes itself, plus an appearance
/// stream holding the poster.
namespace RichMediaWriter {

bool available();

/// Applies `session` to `pdfPath`, replacing the file. `password` is passed in
/// rather than looked up: the staging file inherits the source's encryption
/// but sits under a path the password store knows nothing about.
/// False means the file was left unchanged.
bool apply(const QString &pdfPath, const MediaSession &session,
           const QString &password = QString());

} // namespace RichMediaWriter
