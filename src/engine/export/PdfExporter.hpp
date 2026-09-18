#pragma once

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

bool exportPdf(const QString &sourcePath, const QString &outPath,
               const PdfExportOptions &options);

bool pdfExportAvailable();
