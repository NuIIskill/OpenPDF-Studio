#pragma once

#include "engine/edit/ContentMap.hpp"

#include <QByteArray>
#include <QList>
#include <QMarginsF>
#include <QSizeF>
#include <QString>

/// Holds a picture already compressed for its media part.
struct DocxImage {
    QByteArray data;
    QString    ext;

    bool isNull() const { return data.isEmpty(); }
};

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
    double padLeftPt  { 2.9 };
    double padRightPt { 2.9 };
    QColor ruleColor  { 0xBF, 0xBF, 0xBF };
    double ruleInnerPt { 0.5 };
    double ruleOuterPt { 0.5 };
};

struct DocxBlock {
    enum class Kind { Paragraph, Table, Shape, Picture, TextBox };

    Kind   kind { Kind::Paragraph };
    QRectF bounds;

    QList<ContentItem> lines;
    Qt::Alignment      align { Qt::AlignLeft };
    double             indentPt { 0.0 };
    double             rightIndentPt { 0.0 };
    double             hangingPt { 0.0 };
    QList<double>      leadWordPt;
    double             slackPt { 0.0 };

    DocxTable table;

    DocxImage picture;

    QColor fillColor;
    QColor strokeColor;
    double strokeWidthPt { 0.0 };
};

struct DocxPage {
    QSizeF           pageSizePt;
    QList<ContentItem> items;
    DocxImage        background;
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
                             const QString &title = {});

    static bool exportToDocx(const QString       &outputPath,
                             const QList<QString> &pageTexts,
                             const QString        &title = {});
};
