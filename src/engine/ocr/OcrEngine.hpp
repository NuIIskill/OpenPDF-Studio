#pragma once

#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>

/// OCR engine wrapping Tesseract.
class OcrEngine
{
public:
    struct Block {
        QRectF  pdfBounds;
        QString text;
        bool    isTable { false };
        bool isValid() const { return !text.trimmed().isEmpty() && !pdfBounds.isEmpty(); }
    };

    /// A single cell in a recognised table.
    struct TableCell {
        QRectF  pdfBounds;
        QString text;
    };

    /// One row of a recognised table.
    struct TableRow {
        QList<TableCell> cells;
    };

    /// A complete table found on the page.
    struct Table {
        QRectF          pdfBounds;
        QList<TableRow> rows;
        bool isValid() const { return !rows.isEmpty() && !pdfBounds.isEmpty(); }

        QString toPlainText() const;
    };

    OcrEngine();
    ~OcrEngine();

    OcrEngine(const OcrEngine &)            = delete;
    OcrEngine &operator=(const OcrEngine &) = delete;

    bool isReady() const { return m_ready; }

    QList<Block> recognizePage(const QImage &pageImage,
                                const QSizeF  &pageSizePts,
                                qreal          renderScale) const;

    QList<Table> recognizeTables(const QImage &pageImage,
                                  const QSizeF  &pageSizePts,
                                  qreal          renderScale) const;

    QString recognizeImage(const QImage &image) const;

    QList<QRectF> detectImageRegions(const QImage &pageImage,
                                      const QSizeF  &pageSizePts,
                                      qreal          renderScale) const;

    static Block blockAt(const QList<Block> &blocks,
                          const QPointF      &pdfPt,
                          qreal               maxDistPts = 50.0);

private:
    void *m_api   { nullptr };
    bool  m_ready { false };
};
