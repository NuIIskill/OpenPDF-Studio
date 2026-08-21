#pragma once

#include <QList>
#include <QString>

#include "engine/edit/DocxExporter.hpp"

#ifdef HAVE_PDF_RENDERING
class ContentProvider;
class EditSession;
class OcrEngine;
class PdfRenderer;
class PdfBackend;
#  ifdef HAVE_QT_PRINT
QT_BEGIN_NAMESPACE
class QPrinter;
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
        PdfBackend      *backend    { nullptr };
    };

    explicit DocumentExporter(const Sources &src) : m_src(src) {}
#else
    DocumentExporter() = default;
#endif

    // Positioned, still-editable content per page for the DOCX export.
    // pages holds zero-based indices in output order; empty means every page.
    QList<DocxPage> allPageContent(const QList<int> &pages = {}) const;
    // One PNG per page. quality drives both the render scale and PNG level.
    // Files are numbered by the page's own position in the document, so a
    // range export keeps the names the user expects.
    bool exportPagesToImages(const QString &outputPath, int quality = 85,
                             const QList<int> &pages = {}) const;
#if defined(HAVE_PDF_RENDERING) && defined(HAVE_QT_PRINT)
    // Paints one PDF page per sheet onto an already configured printer, fitted
    // into its printable area. The caller owns the printer and has run the
    // print dialog on it; this only produces the pages.
    // pages holds zero-based indices in output order; empty means every page.
    bool printPages(QPrinter *printer, const QList<int> &pages = {}) const;
#endif

private:

#ifdef HAVE_PDF_RENDERING
    /// `original` with the native glyphs of every recognised text run painted
    /// out, so DOCX text boxes can be placed over it without doubling. Images
    /// and vector graphics survive as the raster layer underneath.
    ///
    /// Exact glyph rectangles need the Qt PDF extractor; without it the item
    /// bounds are erased instead, which is coarser but keeps the Poppler-only
    /// build working.
    QImage eraseTextRuns(const QImage &original, const QList<ContentItem> &items,
                         int page, qreal scale) const;

    Sources m_src;
#endif
};
