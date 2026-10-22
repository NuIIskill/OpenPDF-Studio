#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "fpdfview.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

/// Makes embedded PDF fonts available to Qt.
namespace PdfiumFonts {

struct Face {
    QString family;
    bool    bold   { false };
    bool    italic { false };
};

Face faceOf(FPDF_FONT font);

bool hasFace(const QString &family, bool bold, bool italic);

QString registerWithQt(FPDF_FONT font);

QString familyName(FPDF_FONT font);

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

FPDF_FONT embeddedCopy(FPDF_DOCUMENT doc, FPDF_FONT font);

FPDF_FONT fallbackFont(FPDF_DOCUMENT doc, FPDF_FONT primary, char32_t codePoint);

QHash<QByteArray, QMap<quint32, uint>> writtenGlyphs(FPDF_DOCUMENT doc);

void releaseFonts(FPDF_DOCUMENT doc);

}

#endif
