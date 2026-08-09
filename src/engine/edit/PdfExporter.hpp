#pragma once

#include <QList>
#include <QString>

// ── PDF export options ────────────────────────────────────────────────────────
// Everything the export dialog offers for the PDF target. Applied by
// exportPdf() through qpdf: page selection, annotation and form handling,
// embedded font programs, image recompression and password protection.
struct PdfExportOptions {
    QList<int> pages;                 // zero-based, in order; empty = all pages
    bool       includeComments { true };  // keep non-widget annotations
    bool       keepForms       { true };  // keep AcroForm and widget annotations
    bool       embedFonts      { true };  // keep embedded font programs
    bool       compressImages  { true };  // recompress image XObjects
    int        imageQuality    { 85 };    // JPEG quality, 0-100
    QString    userPassword;              // empty = no encryption
};

// Writes a derived PDF from sourcePath into outPath. Returns false when qpdf
// cannot read the source or the target cannot be written; the caller is then
// expected to fall back to a plain copy/save.
bool exportPdf(const QString &sourcePath, const QString &outPath,
               const PdfExportOptions &options);

// True when this build can honour the options above (qpdf present). Without it
// exportPdf() always fails and callers must use the plain save path.
bool pdfExportAvailable();
