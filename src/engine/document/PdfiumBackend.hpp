#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfBackend.hpp"

#include "fpdf_text.h"
#include "fpdfview.h"

#include <vector>

struct PdfiumChar;
struct PdfiumLine;

/// Implements PDF document operations with PDFium.
class PdfiumBackend : public PdfBackend
{
public:
    PdfiumBackend();
    ~PdfiumBackend() override;

    QString name() const override { return QStringLiteral("PDFium"); }

    bool open(const QString &path, const PasswordAsker &ask) override;
    void close() override;

    int    pageCount() const override;
    QList<PdfBookmark> bookmarks() const override;
    QSizeF pageSizePts(int page) const override;
    QSize  pixelSize(int page, qreal scale) const override;
    QImage renderPage(int page, qreal scale) const override;
    QImage renderPage(int page, qreal scale,
                      const EditSession *session) const override;

    std::unique_ptr<ContentProvider> makeContentProvider() const override;

    bool saveWithEdits(const QString &outputPath,
                       const EditSession &session) const override;

    TextBlock textAt(int page, const QPointF &pdfPt,
                     const QList<QRectF> &exclude = {}) const override;
    TextBlock blockInRect(int page, const QRectF &rect,
                          const QList<QRectF> &exclude = {}) const override;
    QList<QRectF> glyphRects(int page, const QRectF &area,
                             const QList<QRectF> &exclude = {}) const override;
    QString embeddedFontFamily(int page, const QPointF &pdfPt) const override;
    bool    hasSelectableText(int page) const override;

    Selection selectPage(int page, const std::optional<QPointF> &from,
                         const std::optional<QPointF> &to) const override;

private:
    enum class LineSplit {
        Baseline,
        Blocks
    };

    QImage renderPageInternal(int page, qreal scale,
                              const EditSession *session) const;

    /// Die sichtbaren Zeilen einer Seite — die gemeinsame Grundlage aller vier
    /// Textabfragen. Zeichen, deren Mitte in `exclude` liegt, fehlen: das sind
    /// die von der Sitzung überschriebenen Stellen, deren Text als nicht
    /// vorhanden gilt. `from`/`to` beschneiden den Bereich auf zwei Anker;
    /// ohne sie gilt die ganze Seite.
    ///
    /// Macht alles in einem Zug, weil der Zeilentext nur zu haben ist, solange
    /// PDFiums Textseite offen ist.
    std::vector<PdfiumLine> linesOfPage(int page, const QList<QRectF> &exclude,
                                        const std::optional<QPointF> &from,
                                        const std::optional<QPointF> &to,
                                        LineSplit split) const;

    static std::vector<PdfiumLine> buildLines(const std::vector<PdfiumChar> &chars,
                                              int first, int last, LineSplit split,
                                              const QList<QRectF> &textObjects);

    FPDF_DOCUMENT m_doc { nullptr };
};

#endif
