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
        bool isValid() const { return !text.trimmed().isEmpty() && !pdfBounds.isEmpty(); }
    };

    OcrEngine();
    ~OcrEngine();

    OcrEngine(const OcrEngine &)            = delete;
    OcrEngine &operator=(const OcrEngine &) = delete;

    // True when Tesseract was initialised successfully.
    bool isReady() const { return m_ready; }

    // Recognise all text paragraphs on a rendered page.
    //   pageImage   – full page rendered at `renderScale` pixels per PDF point
    //   pageSizePts – page dimensions in PDF points
    //   renderScale – pixels per PDF point used when creating pageImage
    QList<Block> recognizePage(const QImage &pageImage,
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
