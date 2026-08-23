#pragma once

#include "engine/edit/ContentMap.hpp"

#include <QImage>
#include <QList>
#include <QMarginsF>
#include <QSizeF>
#include <QString>

// ── Structured page model ─────────────────────────────────────────────────────
// The default export rebuilds the PDF as native WordprocessingML: flowing
// paragraphs, real tables and editable vector fills/rules. Raster data is kept
// only for genuine graphic regions that Word cannot express directly. A
// positioned page image remains available solely as a scan-only fallback.
//
// DocxLayout.cpp derives these blocks from the ContentItem list plus the
// text-erased page raster; DocxExporter.cpp only serialises them.

struct DocxCell {
    ContentItem   item;          // text + style; empty text = blank cell
    int           row     { 0 };
    int           col     { 0 };
    int           colSpan { 1 };
    QColor        shading;       // invalid = no fill
    Qt::Alignment align { Qt::AlignLeft };
};

struct DocxTable {
    QList<double>   colWidthsPt;  // one entry per column, left to right
    QList<double>   rowHeightsPt; // one entry per row — keeps the PDF's pitch
    QList<DocxCell> cells;
    int  rowCount   { 0 };
    bool hasBorders { true };
    double indentPt { 0.0 };     // offset of the table's left edge from margin
};

struct DocxBlock {
    enum class Kind { Paragraph, Table, Shape, Picture, TextBox };

    Kind   kind { Kind::Paragraph };
    QRectF bounds;               // page-space pt, drives order and spacing

    // Paragraph
    QList<ContentItem> lines;    // consecutive lines of one logical paragraph
    Qt::Alignment      align { Qt::AlignLeft };
    double             indentPt { 0.0 };

    // Table
    DocxTable table;

    // Picture
    QImage picture;              // already cropped to bounds

    // Native anchored objects. Shapes carry flat panels/header bands/rules;
    // TextBox uses lines for editable labels sitting inside a graphic region.
    QColor fillColor;
    QColor strokeColor;
    double strokeWidthPt { 0.0 };
};

struct DocxPage {
    QSizeF           pageSizePt;
    QList<ContentItem> items;    // positioned, editable text runs
    QImage           background; // analysis/scan fallback, not the normal page layer
    QList<DocxBlock> blocks;     // native structured Word representation
    QMarginsF        marginsPt { 56.0, 45.0, 56.0, 45.0 };
};

// What the export dialog can steer for the Word target. Only the parts a DOCX
// can actually express are here — a .docx has no equivalent of PDF encryption
// or of a PDF's embedded font programs, so those stay out rather than being
// offered and silently ignored.
struct DocxExportOptions {
    bool compressImages { true };   // permit JPEG when smaller; otherwise keep PNG
    int  imageQuality   { 85 };     // drives both the picture scale and JPEG level
};

// Exports PDF content as an editable .docx file.
// Pure C++/Qt — no external tools or libraries required.
class DocxExporter
{
public:
    static bool exportToDocx(const QString &outputPath,
                             const QList<DocxPage> &pages,
                             const QString &title = {},
                             const DocxExportOptions &options = {});

    // Compatibility entry point for callers which have no page geometry.
    static bool exportToDocx(const QString       &outputPath,
                             const QList<QString> &pageTexts,
                             const QString        &title = {});
};
