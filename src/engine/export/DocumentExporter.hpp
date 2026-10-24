#pragma once

#include <QImage>
#include <QList>
#include <QString>

#include <functional>

#include "engine/export/DocxExporter.hpp"

#ifdef HAVE_PDF_RENDERING
class ContentProvider;
class EditSession;
class OcrEngine;
class PdfRenderer;
class PdfBackend;
#endif

/// Pulls page content out of renderer + session for the DOCX and image exports.
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

    QList<DocxPage> allPageContent(const QList<int> &pages,
                                   const DocxExportOptions &options,
                                   const std::function<bool(int, int)> &progress = {}) const;

    bool exportPagesToImages(const QString &outputPath, int quality = 85,
                             const QList<int> &pages = {},
                             const std::function<bool(int, int)> &progress = {}) const;

private:

#ifdef HAVE_PDF_RENDERING

    QImage eraseTextRuns(const QImage &original, const QList<ContentItem> &items,
                         int page, qreal scale) const;

    Sources m_src;
#endif
};
