#pragma once

#include "engine/document/WriteGrant.hpp"
#include <QList>
#include <QString>

/// Stores PDF export options.
struct PdfExportOptions {
    QList<int> pages;
    bool       includeComments { true };
    bool       keepForms       { true };
    bool       embedFonts      { true };
    bool       compressImages  { true };
    int        imageQuality    { 85 };
    QString    userPassword;
};

bool exportPdf(const QString &sourcePath, const WriteGrant &outPath,
               const PdfExportOptions &options);

bool pdfExportAvailable();
