#pragma once

#include <QList>
#include <QString>

#include "DocxExporter.hpp"

#ifdef HAVE_PDF_RENDERING
class ContentProvider;
class EditSession;
class OcrEngine;
class PdfRenderer;
#  ifdef HAVE_QT_PDF
class PdfTextExtractor;
QT_BEGIN_NAMESPACE
class QPdfDocument;
QT_END_NAMESPACE
#  endif
#endif

// Pulls page content out of renderer + session for the DOCX and image exports.
//
// Deliberately free of any UI dependency — this used to live in DocumentView
// although it touches no widget, no event and no zoom level. That also makes
// it directly testable without a running view.
//
// Holds only borrowed pointers; the caller keeps ownership and must not let
// the exporter outlive them.
class DocumentExporter
{
public:
#ifdef HAVE_PDF_RENDERING
    struct Sources {
        PdfRenderer     *renderer  { nullptr };
        ContentProvider *provider  { nullptr };
        EditSession     *session   { nullptr };
        OcrEngine       *ocr       { nullptr };
        int              pageCount { 0 };
#  ifdef HAVE_QT_PDF
        QPdfDocument     *document  { nullptr };
        PdfTextExtractor *extractor { nullptr };
#  endif
    };

    explicit DocumentExporter(const Sources &src) : m_src(src) {}
#else
    DocumentExporter() = default;
#endif

    // Positioned, still-editable content per page for the DOCX export.
    QList<DocxPage> allPageContent() const;
    // One PNG per page. quality drives both the render scale and PNG level.
    bool exportPagesToImages(const QString &outputPath, int quality = 85) const;

private:
#ifdef HAVE_PDF_RENDERING
    Sources m_src;
#endif
};
