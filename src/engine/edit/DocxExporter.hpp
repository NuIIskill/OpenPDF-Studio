#pragma once

#include "ContentMap.hpp"

#include <QImage>
#include <QList>
#include <QMarginsF>
#include <QSizeF>
#include <QString>

// ── Structured page model ─────────────────────────────────────────────────────
// The export writes real WordprocessingML structure — flowing paragraphs, real
// w:tbl tables with cell shading, embedded pictures — rather than a page-sized
// raster with absolutely positioned text boxes on top. Only content that has no
// word-processor equivalent (vector art, charts) stays a picture, cropped to its
// own bounding box instead of covering the whole page.
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
    enum class Kind { Paragraph, Table, Picture };

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
};

struct DocxPage {
    QSizeF           pageSizePt;
    QList<ContentItem> items;    // per-line items — positioned fallback path
    QImage           background; // full-page raster — positioned fallback path
    QList<DocxBlock> blocks;     // structured flow; empty = use fallback
    QMarginsF        marginsPt { 56.0, 45.0, 56.0, 45.0 };
};

// Exports PDF content as an editable .docx file.
// Pure C++/Qt — no external tools or libraries required.
// Uses ZIP store compression (method=0), accepted by all Word/LibreOffice versions.
class DocxExporter
{
public:
    static bool exportToDocx(const QString &outputPath,
                             const QList<DocxPage> &pages,
                             const QString &title = {});

    // Compatibility entry point for callers which have no page geometry.
    static bool exportToDocx(const QString       &outputPath,
                             const QList<QString> &pageTexts,
                             const QString        &title = {});
};
