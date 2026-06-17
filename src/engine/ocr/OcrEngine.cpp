#include "OcrEngine.hpp"

#ifdef HAVE_TESSERACT
#  include <tesseract/baseapi.h>
#  include <leptonica/allheaders.h>
#endif

#include <QDebug>
#include <algorithm>
#include <cmath>
#include <limits>

// ── Construction / destruction ────────────────────────────────────────────────

OcrEngine::OcrEngine()
{
#ifdef HAVE_TESSERACT
    auto *api = new tesseract::TessBaseAPI();

    // Try German + English first; fall back to English-only when deu data is absent.
    if (api->Init(nullptr, "deu+eng") == 0) {
        qDebug() << "[OCR] Tesseract ready (deu+eng)";
        m_api   = api;
        m_ready = true;
    } else if (api->Init(nullptr, "eng") == 0) {
        qDebug() << "[OCR] Tesseract ready (eng only — install tesseract-langpack-deu for German)";
        m_api   = api;
        m_ready = true;
    } else {
        qWarning() << "[OCR] Tesseract init failed — install tesseract + tesseract-langpack-eng";
        delete api;
    }
#endif
}

OcrEngine::~OcrEngine()
{
#ifdef HAVE_TESSERACT
    if (m_api) {
        auto *api = static_cast<tesseract::TessBaseAPI *>(m_api);
        api->End();
        delete api;
    }
#endif
}

// ── Page recognition ──────────────────────────────────────────────────────────

QList<OcrEngine::Block> OcrEngine::recognizePage(const QImage &pageImage,
                                                   const QSizeF  &pageSizePts,
                                                   qreal          renderScale) const
{
    QList<Block> result;

#ifdef HAVE_TESSERACT
    if (!m_ready || pageImage.isNull() || renderScale <= 0.0) return result;

    auto *api = static_cast<tesseract::TessBaseAPI *>(m_api);

    // Tesseract requires a packed RGB image; convert once.
    const QImage img = pageImage.convertToFormat(QImage::Format_RGB888);

    // bytes-per-pixel, bytes-per-line
    const int bpp  = img.depth() / 8;
    const int bpl  = static_cast<int>(img.bytesPerLine());

    api->SetImage(img.bits(), img.width(), img.height(), bpp, bpl);
    api->SetPageSegMode(tesseract::PSM_AUTO);

    if (api->Recognize(nullptr) != 0) {
        qWarning() << "[OCR] Recognize() failed";
        api->Clear();
        return result;
    }

    tesseract::ResultIterator *ri = api->GetIterator();
    if (!ri) { api->Clear(); return result; }

    // Iterate at line level — gives tighter bounding boxes than paragraph level,
    // so hit-testing against a click point is much more accurate.
    const tesseract::PageIteratorLevel level = tesseract::RIL_TEXTLINE;
    const QRectF pageRect(QPointF(0, 0), pageSizePts);

    do {
        const char *raw = ri->GetUTF8Text(level);
        if (!raw) continue;
        const QString text = QString::fromUtf8(raw).trimmed();
        delete[] raw;
        if (text.isEmpty()) continue;

        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (!ri->BoundingBox(level, &x1, &y1, &x2, &y2)) continue;

        // Pixel coordinates (top-left origin) → PDF points (top-left origin)
        const QRectF bounds(x1 / renderScale, y1 / renderScale,
                             (x2 - x1) / renderScale, (y2 - y1) / renderScale);

        const QRectF clamped = bounds.intersected(pageRect);
        if (clamped.isEmpty()) continue;

        result.append({ clamped, text });

    } while (ri->Next(level));

    delete ri;
    api->Clear();
    qDebug() << "[OCR] Page recognition complete:" << result.size() << "lines found";
#else
    Q_UNUSED(pageImage)
    Q_UNUSED(pageSizePts)
    Q_UNUSED(renderScale)
#endif

    return result;
}

// ── Hit-testing ───────────────────────────────────────────────────────────────

OcrEngine::Block OcrEngine::blockAt(const QList<Block> &blocks,
                                      const QPointF      &pdfPt,
                                      qreal               maxDistPts)
{
    if (blocks.isEmpty()) return {};

    Block  best;
    qreal  bestDist = std::numeric_limits<qreal>::max();

    for (const Block &b : blocks) {
        if (b.pdfBounds.contains(pdfPt))
            return b;

        // Distance from point to nearest edge of the rectangle
        const qreal dx = std::max({ 0.0, b.pdfBounds.left()   - pdfPt.x(),
                                         pdfPt.x() - b.pdfBounds.right()  });
        const qreal dy = std::max({ 0.0, b.pdfBounds.top()    - pdfPt.y(),
                                         pdfPt.y() - b.pdfBounds.bottom() });
        const qreal dist = std::hypot(dx, dy);
        if (dist < bestDist) { bestDist = dist; best = b; }
    }

    return (bestDist <= maxDistPts) ? best : Block{};
}
