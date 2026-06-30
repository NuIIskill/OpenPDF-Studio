#pragma once

#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>

// OCR engine wrapping Tesseract.  The header is always available regardless of
// whether HAVE_TESSERACT is defined — callers can include it unconditionally.
// When Tesseract is not available, isReady() returns false and recognizePage()
// returns an empty list.
class OcrEngine
{
public:
    struct Block {
        QRectF  pdfBounds;   // bounding box in PDF points (origin top-left)
        QString text;
        bool    isTable { false };
        bool isValid() const { return !text.trimmed().isEmpty() && !pdfBounds.isEmpty(); }
    };

    // A single cell in a recognised table.
    struct TableCell {
        QRectF  pdfBounds;
        QString text;
    };

    // One row of a recognised table.
    struct TableRow {
        QList<TableCell> cells;
    };

    // A complete table found on the page.
    struct Table {
        QRectF          pdfBounds;
        QList<TableRow> rows;
        bool isValid() const { return !rows.isEmpty() && !pdfBounds.isEmpty(); }
        // Returns all cell texts joined with tabs (rows separated by newlines).
        QString toPlainText() const;
    };

    OcrEngine();
    ~OcrEngine();

    OcrEngine(const OcrEngine &)            = delete;
    OcrEngine &operator=(const OcrEngine &) = delete;

    // True when Tesseract was initialised successfully.
    bool isReady() const { return m_ready; }

    // Recognise all text lines on a rendered page.
    QList<Block> recognizePage(const QImage &pageImage,
                                const QSizeF  &pageSizePts,
                                qreal          renderScale) const;

    // Recognise tables on a rendered page.
    // Returns one Table per detected table block; each Table contains rows of cells.
    QList<Table> recognizeTables(const QImage &pageImage,
                                  const QSizeF  &pageSizePts,
                                  qreal          renderScale) const;

    // Run OCR on an arbitrary QImage (e.g. an inserted image annotation).
    // Returns plain text; empty string when nothing is found or Tesseract unavailable.
    QString recognizeImage(const QImage &image) const;

    // Detect regions on a page that contain embedded images (not text).
    // Uses Tesseract layout analysis — no full OCR pass needed.
    // Returns bounding boxes in PDF points (top-left origin).
    QList<QRectF> detectImageRegions(const QImage &pageImage,
                                      const QSizeF  &pageSizePts,
                                      qreal          renderScale) const;

    // Find the block that contains pdfPt, or the nearest one within maxDistPts.
    // Returns an invalid Block if the list is empty or nothing is close enough.
    static Block blockAt(const QList<Block> &blocks,
                          const QPointF      &pdfPt,
                          qreal               maxDistPts = 50.0);

private:
    void *m_api   { nullptr }; // TessBaseAPI* – void* avoids polluting the header
    bool  m_ready { false };
};
