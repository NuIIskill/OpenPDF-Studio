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

    QList<DocxPage> allPageContent(const QList<int> &pages = {}) const;

    bool exportPagesToImages(const QString &outputPath, int quality = 85,
                             const QList<int> &pages = {}) const;
#if defined(HAVE_PDF_RENDERING) && defined(HAVE_QT_PRINT)

    bool printPages(QPrinter *printer, const QList<int> &pages = {}) const;
#endif

private:

#ifdef HAVE_PDF_RENDERING

    QImage eraseTextRuns(const QImage &original, const QList<ContentItem> &items,
                         int page, qreal scale) const;

    Sources m_src;
#endif
};
