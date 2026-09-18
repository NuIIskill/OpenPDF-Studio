#pragma once

#include "engine/edit/ContentMap.hpp"

#include <QImage>
#include <QList>
#include <QMarginsF>
#include <QSizeF>
#include <QString>

/// Stores one block in the generated WordprocessingML document.
struct DocxCell {
    ContentItem   item;
    int           row     { 0 };
    int           col     { 0 };
    int           colSpan { 1 };
    QColor        shading;
    Qt::Alignment align { Qt::AlignLeft };
};

struct DocxTable {
    QList<double>   colWidthsPt;
    QList<double>   rowHeightsPt;
    QList<DocxCell> cells;
    int  rowCount   { 0 };
    bool hasBorders { true };
    double indentPt { 0.0 };
};

struct DocxBlock {
    enum class Kind { Paragraph, Table, Shape, Picture, TextBox };

    Kind   kind { Kind::Paragraph };
    QRectF bounds;

    QList<ContentItem> lines;
    Qt::Alignment      align { Qt::AlignLeft };
    double             indentPt { 0.0 };

    DocxTable table;

    QImage picture;

    QColor fillColor;
    QColor strokeColor;
    double strokeWidthPt { 0.0 };
};

struct DocxPage {
    QSizeF           pageSizePt;
    QList<ContentItem> items;
    QImage           background;
    QList<DocxBlock> blocks;
    QMarginsF        marginsPt { 56.0, 45.0, 56.0, 45.0 };
};

/// What the export dialog can steer for the Word target.
struct DocxExportOptions {
    bool compressImages { true };
    int  imageQuality   { 85 };
};

/// Exports PDF content as an editable .docx file.
class DocxExporter
{
public:
    static bool exportToDocx(const QString &outputPath,
                             const QList<DocxPage> &pages,
                             const QString &title = {},
                             const DocxExportOptions &options = {});

    static bool exportToDocx(const QString       &outputPath,
                             const QList<QString> &pageTexts,
                             const QString        &title = {});
};
