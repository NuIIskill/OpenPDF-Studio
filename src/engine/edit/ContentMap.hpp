#pragma once

#include <QColor>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

// ── ContentItem ───────────────────────────────────────────────────────────────
// One logical region detected on a PDF page. Backend-neutral: produced by the
// qpdf content-stream scanner (ContentMap.cpp) or the Poppler word-box path
// (ContentModel.cpp). New region types (video, vector art, …) extend the enum;
// a provider that cannot detect a type simply never emits it.
struct ContentItem {
    enum class Type {
        Text,        // single text line (or isolated segment)
        Paragraph,   // multi-line text block — edited as one unit
        TableCell,   // one cell of a detected table row
        FormField,   // AcroForm widget (fillable area)
        Image,       // raster image placement (XObject)
        Media,       // video/sound annotation (Screen, RichMedia, Movie)
    };

    Type    type       { Type::Text };
    QRectF  bounds;          // page-space PDF points, Y=0 at top
    QString text;            // text content, lines joined with '\n'
    double  fontSizePt { 0.0 };
    QString fontFamily;      // resolved Qt font family ("" = unknown)
    QString rawFontName;     // PDF BaseFont as written in the file
    bool    bold       { false };
    bool    italic     { false };
    QColor  textColor;       // text fill color (invalid = unknown)
    QColor  bgColor;         // background fill behind the text (invalid = none)
    QString fieldName;       // AcroForm /T (FormField only)

    bool isFormField() const { return type == Type::FormField; }
    bool isTextual()   const {
        return type == Type::Text || type == Type::Paragraph
            || type == Type::TableCell || type == Type::FormField;
    }
    bool isValid()     const { return !bounds.isNull() && !bounds.isEmpty(); }
};

// Type bitmask for spatial lookups.
constexpr unsigned contentTypeBit(ContentItem::Type t)
{
    return 1u << static_cast<unsigned>(t);
}
constexpr unsigned kTextualContentTypes =
      contentTypeBit(ContentItem::Type::Text)
    | contentTypeBit(ContentItem::Type::Paragraph)
    | contentTypeBit(ContentItem::Type::TableCell)
    | contentTypeBit(ContentItem::Type::FormField);
constexpr unsigned kAllContentTypes = 0x3Fu;

// Spatial lookup over classified items: exact containment (3 pt tolerance,
// FormField > TableCell > Text/Paragraph priority, smaller area wins ties),
// then nearest edge within maxDistance pt. Returns invalid item if none.
ContentItem contentItemAt(const QList<ContentItem> &items, const QPointF &pdfPt,
                          unsigned typeMask = kTextualContentTypes,
                          double maxDistance = 40.0);

// ── Shared geometry classifier ────────────────────────────────────────────────
// Backend-neutral intermediate: one positioned text run (a word from Poppler
// or one Tj/TJ show-op from the qpdf scanner).
struct ContentCluster {
    QRectF  bounds;          // page-space, Y=0 at top
    QString text;
    double  fontSizePt { 0.0 };
    QString rawFontName;     // PDF BaseFont (may be empty)
    QColor  textColor;
    bool    exactWidth { false };  // true when bounds.width is glyph-exact
};

// Groups runs into lines → segments, detects table rows via column alignment
// across neighbouring rows, merges aligned consecutive lines into Paragraph
// items (text joined with '\n'). Font/color metadata is carried through.
QList<ContentItem> classifyContentClusters(QList<ContentCluster> clusters);

// ── Font resolution ───────────────────────────────────────────────────────────
struct ResolvedFont {
    QString family;          // Qt font family (alias-mapped, camel-case split)
    bool    bold   { false };
    bool    italic { false };
};
// "ABCDEF+Helvetica-BoldOblique" → { "Helvetica", bold=true, italic=true }
ResolvedFont resolvePdfFont(const QString &rawBaseFont);

// ── qpdf content-stream scanner ───────────────────────────────────────────────
#ifdef HAVE_QPDF

#include <string>
#include <qpdf/QPDFObjectHandle.hh>

// Scans one page: text clusters (CTM-corrected, font + color resolved, one
// level of Form-XObject recursion), background fills, image placements, and
// AcroForm/media annotations. Returns the fully classified item list.
QList<ContentItem> qpdfBuildPageItems(const std::string &cs, double pageH,
                                      QPDFObjectHandle pageObj);

#endif // HAVE_QPDF
