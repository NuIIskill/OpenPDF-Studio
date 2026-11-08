#pragma once

#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTransform>

#include <functional>

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

    struct Word {
        QRectF        pdfBounds;
        QString       text;
        QList<QRectF> chars;
        qreal         baseline  { 0.0 };
        int           paragraph { 0 };
    };
    using Line = QList<Word>;

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

    void refresh();

    QList<Block> recognizePage(const QImage &pageImage,
                                const QSizeF  &pageSizePts,
                                qreal          renderScale) const;

    QList<Line> recognizeLines(const QImage &pageImage,
                               const QSizeF  &pageSizePts,
                               qreal          renderScale,
                               bool           settle = false,
                               const std::function<bool()> &cancelled = {},
                               const std::function<void(int percent)> &progress = {}) const;

    QList<Table> recognizeTables(const QImage &pageImage,
                                  const QSizeF  &pageSizePts,
                                  qreal          renderScale) const;

    QString recognizeImage(const QImage &image) const;

    QString recognizeLine(const QImage &image, int dpi) const;

    static Block blockAt(const QList<Block> &blocks,
                          const QPointF      &pdfPt,
                          qreal               maxDistPts = 50.0);

private:
    void release();
    QTransform setPage(const QImage &pageImage, qreal renderScale, QImage *straight = nullptr) const;

    void   *m_api   { nullptr };
    bool    m_ready { false };
    bool    m_tried { false };
    QString m_languages;
};
