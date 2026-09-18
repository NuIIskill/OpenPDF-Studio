#include "engine/ocr/OcrEngine.hpp"

#ifdef HAVE_TESSERACT
#  include <tesseract/baseapi.h>
#  include <leptonica/allheaders.h>
#endif

#include <QDebug>
#include <algorithm>
#include <cmath>
#include <limits>

OcrEngine::OcrEngine()
{
#ifdef HAVE_TESSERACT
    auto *api = new tesseract::TessBaseAPI();

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

QList<OcrEngine::Block> OcrEngine::recognizePage(const QImage &pageImage,
                                                   const QSizeF  &pageSizePts,
                                                   qreal          renderScale) const
{
    QList<Block> result;

#ifdef HAVE_TESSERACT
    if (!m_ready || pageImage.isNull() || renderScale <= 0.0) return result;

    auto *api = static_cast<tesseract::TessBaseAPI *>(m_api);

    const QImage img = pageImage.convertToFormat(QImage::Format_RGB888);

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

QString OcrEngine::Table::toPlainText() const
{
    QString out;
    for (const TableRow &row : rows) {
        QStringList parts;
        for (const TableCell &cell : row.cells)
            parts << cell.text;
        out += parts.join(QLatin1Char('\t')) + QLatin1Char('\n');
    }
    return out.trimmed();
}

QList<OcrEngine::Table> OcrEngine::recognizeTables(const QImage &pageImage,
                                                     const QSizeF  &pageSizePts,
                                                     qreal          renderScale) const
{
    QList<Table> result;

#ifdef HAVE_TESSERACT
    if (!m_ready || pageImage.isNull() || renderScale <= 0.0) return result;

    auto *api = static_cast<tesseract::TessBaseAPI *>(m_api);

    const QImage img = pageImage.convertToFormat(QImage::Format_RGB888);
    api->SetImage(img.bits(), img.width(), img.height(),
                  img.depth() / 8, static_cast<int>(img.bytesPerLine()));
    api->SetPageSegMode(tesseract::PSM_AUTO);

    if (api->Recognize(nullptr) != 0) {
        api->Clear();
        return result;
    }

    const QRectF pageRect(QPointF(0, 0), pageSizePts);

    /// Stores a detected table region in image coordinates.
    struct TableRegion { int x1, y1, x2, y2; };
    QList<TableRegion> tableRegions;
    {
        tesseract::ResultIterator *ri = api->GetIterator();
        if (ri) {
            do {
                if (ri->BlockType() == tesseract::PT_TABLE) {
                    int bx1 = 0, by1 = 0, bx2 = 0, by2 = 0;
                    if (ri->BoundingBox(tesseract::RIL_BLOCK, &bx1, &by1, &bx2, &by2))
                        tableRegions.append({ bx1, by1, bx2, by2 });
                }
            } while (ri->Next(tesseract::RIL_BLOCK));
            delete ri;
        }
    }

    if (tableRegions.isEmpty()) {
        api->Clear();
        return result;
    }

    for (const TableRegion &tr : tableRegions) {
        tesseract::ResultIterator *ri = api->GetIterator();
        if (!ri) continue;

        /// Stores an OCR word and its image bounds.
        struct Word { int x1, y1, x2, y2; QString text; };
        QList<Word> words;

        do {
            int bx1 = 0, by1 = 0, bx2 = 0, by2 = 0;
            if (!ri->BoundingBox(tesseract::RIL_BLOCK, &bx1, &by1, &bx2, &by2)) continue;
            if (bx1 != tr.x1 || by1 != tr.y1) continue;

            do {
                const char *raw = ri->GetUTF8Text(tesseract::RIL_WORD);
                if (!raw) { if (ri->IsAtFinalElement(tesseract::RIL_BLOCK, tesseract::RIL_WORD)) break; continue; }
                const QString text = QString::fromUtf8(raw).trimmed();
                delete[] raw;

                int wx1 = 0, wy1 = 0, wx2 = 0, wy2 = 0;
                if (!text.isEmpty() && ri->BoundingBox(tesseract::RIL_WORD, &wx1, &wy1, &wx2, &wy2))
                    words.append({ wx1, wy1, wx2, wy2, text });

                if (ri->IsAtFinalElement(tesseract::RIL_BLOCK, tesseract::RIL_WORD)) break;
            } while (ri->Next(tesseract::RIL_WORD));
            break;
        } while (ri->Next(tesseract::RIL_BLOCK));
        delete ri;

        if (words.isEmpty()) continue;

        const int rowTol = [&]() {
            int sumH = 0;
            for (const Word &w : words) sumH += (w.y2 - w.y1);
            return qMax(4, sumH / words.size() / 2);
        }();

        std::sort(words.begin(), words.end(), [](const Word &a, const Word &b) {
            return a.y1 < b.y1 || (a.y1 == b.y1 && a.x1 < b.x1);
        });

        QList<QList<Word>> rowGroups;
        for (const Word &w : words) {
            const int yc = (w.y1 + w.y2) / 2;
            bool placed = false;
            for (auto &rg : rowGroups) {
                const int rgYc = (rg.first().y1 + rg.first().y2) / 2;
                if (qAbs(yc - rgYc) <= rowTol) {
                    rg.append(w);
                    placed = true;
                    break;
                }
            }
            if (!placed) rowGroups.append({ w });
        }

        for (auto &rg : rowGroups)
            std::sort(rg.begin(), rg.end(), [](const Word &a, const Word &b) {
                return a.x1 < b.x1;
            });

        Table table;
        table.pdfBounds = QRectF(tr.x1 / renderScale, tr.y1 / renderScale,
                                  (tr.x2 - tr.x1) / renderScale,
                                  (tr.y2 - tr.y1) / renderScale)
                          .intersected(pageRect);

        for (const auto &rg : rowGroups) {

            const int avgW = [&]() {
                int sum = 0;
                for (const Word &w : rg) sum += (w.x2 - w.x1);
                return qMax(1, sum / rg.size());
            }();
            const int cellGapThresh = avgW * 2;

            TableRow row;
            QString cellText;
            QRectF  cellBounds;

            for (int wi = 0; wi < rg.size(); ++wi) {
                const Word &w = rg[wi];
                const QRectF wb(w.x1 / renderScale, w.y1 / renderScale,
                                (w.x2 - w.x1) / renderScale, (w.y2 - w.y1) / renderScale);

                if (!cellText.isEmpty() && (w.x1 - rg[wi - 1].x2) > cellGapThresh) {
                    row.cells.append({ cellBounds.intersected(pageRect), cellText.trimmed() });
                    cellText.clear();
                    cellBounds = {};
                }
                cellText   += (cellText.isEmpty() ? QString() : QStringLiteral(" ")) + w.text;
                cellBounds  = cellBounds.isEmpty() ? wb : cellBounds.united(wb);
            }
            if (!cellText.isEmpty())
                row.cells.append({ cellBounds.intersected(pageRect), cellText.trimmed() });

            if (!row.cells.isEmpty())
                table.rows.append(row);
        }

        if (table.isValid())
            result.append(table);
    }

    api->Clear();
    qDebug() << "[OCR] Table recognition complete:" << result.size() << "table(s)";
#else
    Q_UNUSED(pageImage)
    Q_UNUSED(pageSizePts)
    Q_UNUSED(renderScale)
#endif

    return result;
}

QString OcrEngine::recognizeImage(const QImage &image) const
{
#ifdef HAVE_TESSERACT
    if (!m_ready || image.isNull()) return {};

    auto *api = static_cast<tesseract::TessBaseAPI *>(m_api);

    const QImage img = image.convertToFormat(QImage::Format_RGB888);
    api->SetImage(img.bits(), img.width(), img.height(),
                  img.depth() / 8, static_cast<int>(img.bytesPerLine()));
    api->SetPageSegMode(tesseract::PSM_AUTO);

    char *raw = api->GetUTF8Text();
    api->Clear();
    if (!raw) return {};
    const QString text = QString::fromUtf8(raw).trimmed();
    delete[] raw;
    qDebug() << "[OCR] Image recognition:" << text.left(60);
    return text;
#else
    Q_UNUSED(image)
    return {};
#endif
}

QList<QRectF> OcrEngine::detectImageRegions(const QImage &pageImage,
                                              const QSizeF  &pageSizePts,
                                              qreal          renderScale) const
{
    QList<QRectF> result;

#ifdef HAVE_TESSERACT
    if (!m_ready || pageImage.isNull() || renderScale <= 0.0) return result;

    auto *api = static_cast<tesseract::TessBaseAPI *>(m_api);

    const QImage img = pageImage.convertToFormat(QImage::Format_RGB888);
    api->SetImage(img.bits(), img.width(), img.height(),
                  img.depth() / 8, static_cast<int>(img.bytesPerLine()));

    tesseract::PageIterator *pi = api->AnalyseLayout();
    if (!pi) { api->Clear(); return result; }

    const QRectF pageRect(QPointF(0, 0), pageSizePts);

    do {
        const tesseract::PolyBlockType bt = pi->BlockType();
        if (bt == tesseract::PT_FLOWING_IMAGE ||
            bt == tesseract::PT_HEADING_IMAGE  ||
            bt == tesseract::PT_PULLOUT_IMAGE) {
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (!pi->BoundingBox(tesseract::RIL_BLOCK, &x1, &y1, &x2, &y2)) continue;
            const QRectF r(x1 / renderScale, y1 / renderScale,
                           (x2 - x1) / renderScale, (y2 - y1) / renderScale);
            const QRectF clamped = r.intersected(pageRect);
            if (!clamped.isEmpty() && clamped.width() > 10 && clamped.height() > 10)
                result.append(clamped);
        }
    } while (pi->Next(tesseract::RIL_BLOCK));

    delete pi;
    api->Clear();
    qDebug() << "[OCR] Image region detection:" << result.size() << "region(s)";
#else
    Q_UNUSED(pageImage)
    Q_UNUSED(pageSizePts)
    Q_UNUSED(renderScale)
#endif

    return result;
}

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

        const qreal dx = std::max({ 0.0, b.pdfBounds.left()   - pdfPt.x(),
                                         pdfPt.x() - b.pdfBounds.right()  });
        const qreal dy = std::max({ 0.0, b.pdfBounds.top()    - pdfPt.y(),
                                         pdfPt.y() - b.pdfBounds.bottom() });
        const qreal dist = std::hypot(dx, dy);
        if (dist < bestDist) { bestDist = dist; best = b; }
    }

    return (bestDist <= maxDistPts) ? best : Block{};
}
