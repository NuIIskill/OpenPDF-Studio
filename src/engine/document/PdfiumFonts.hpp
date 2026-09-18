#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "fpdfview.h"

#include <QByteArray>
#include <QString>

/// Makes embedded PDF fonts available to Qt.
namespace PdfiumFonts {

QString registerWithQt(FPDF_FONT font);

QByteArray standardFontFor(const QString &family, bool bold, bool italic);

QByteArray fontData(const QString &family, bool bold, bool italic);

}

#endif
