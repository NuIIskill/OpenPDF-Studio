#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "fpdfview.h"

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>

/// Makes embedded PDF fonts available to Qt.
namespace PdfiumFonts {

QString registerWithQt(FPDF_FONT font);

QByteArray standardFontFor(const QString &family, bool bold, bool italic);

QByteArray fontData(const QString &family, bool bold, bool italic);

bool isType3(FPDF_FONT font);

bool hasGlyph(FPDF_FONT font, char32_t codePoint);

bool isLoaded(FPDF_FONT font);

QSet<uint> decodable(FPDF_DOCUMENT doc, FPDF_PAGE page, FPDF_FONT font,
                     const QList<uint> &codePoints);

double glyphWidth(FPDF_FONT font, char32_t codePoint, double sizePt);

void setText(FPDF_PAGEOBJECT object, FPDF_FONT font, const QString &text);

FPDF_FONT loadFont(FPDF_DOCUMENT doc, const QString &family, bool bold, bool italic);

FPDF_FONT fallbackFont(FPDF_DOCUMENT doc, FPDF_FONT primary, char32_t codePoint);

void releaseFonts(FPDF_DOCUMENT doc);

}

#endif
