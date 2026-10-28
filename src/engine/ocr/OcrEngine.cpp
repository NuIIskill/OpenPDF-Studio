#include "engine/ocr/OcrEngine.hpp"
#include "engine/ocr/OcrLanguages.hpp"

#ifdef HAVE_TESSERACT
#  include <tesseract/baseapi.h>
#  include <leptonica/allheaders.h>
#endif

#include <QDebug>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>

OcrEngine::OcrEngine()
{
    refresh();
}

OcrEngine::~OcrEngine()
{
    release();
}

void OcrEngine::refresh()
{
#ifdef HAVE_TESSERACT
    const QString wanted = OcrLanguages::installed().join(u'+');
    if (m_tried && wanted == m_languages) return;
    m_tried     = true;
    m_languages = wanted;
    release();

    auto *api = new tesseract::TessBaseAPI();

    // The Windows packages carry the language data next to the executable;
    // the path compiled into Tesseract points into the build machine's sysroot.
#ifdef Q_OS_WIN
    const QByteArray dataDir = (OcrLanguages::dataDir() + u'/').toLocal8Bit();
    const char *dataPath = dataDir.constData();
#else
    const char *dataPath = nullptr;
#endif

    // Languages kept somewhere OcrLanguages does not look are still found by
    // Tesseract's own search path.
    const QStringList attempts = wanted.isEmpty()
        ? QStringList { QStringLiteral("deu+eng+fra"), QStringLiteral("eng") }
        : QStringList { wanted };
    for (const QString &languages : attempts) {
        if (api->Init(dataPath, languages.toLatin1().constData()) != 0) continue;
        // A missing language is skipped, so report what was actually loaded.
        std::vector<std::string> loaded;
        api->GetLoadedLanguagesAsVector(&loaded);
        QStringList names;
        for (const std::string &lang : loaded)
            names << QString::fromStdString(lang);
        qDebug() << "[OCR] Tesseract ready:" << names.join(u'+');
        m_api   = api;
        m_ready = true;
        return;
    }
    qWarning() << "[OCR] Tesseract init failed, no language data installed";
    delete api;
#endif
}

void OcrEngine::release()
{
#ifdef HAVE_TESSERACT
    if (m_api) {
        auto *api = static_cast<tesseract::TessBaseAPI *>(m_api);
        api->End();
        delete api;
    }
#endif
    m_api   = nullptr;
    m_ready = false;
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

    const QRectF pageRect(QPointF(0, 0), pageSizePts);

    // A line is split where its words stand far apart, as table cells do: a
    // gap wider than two and a half times the line's height.
    struct Word { QRectF box; QString text; };
    QList<Word> line;
    const auto flush = [&]() {
        double height = 0.0;
        for (const Word &w : std::as_const(line)) height = std::max(height, w.box.height());
        QRectF box;
        QString text;
        for (int i = 0; i < line.size(); ++i) {
            const Word &w = line.at(i);
            if (i > 0 && w.box.left() - line.at(i - 1).box.right() > height * 2.5) {
                if (!text.isEmpty()) result.append({ box, text });
                box = QRectF();
                text.clear();
            }
            box = box.isNull() ? w.box : box.united(w.box);
            text += (text.isEmpty() ? QString() : QStringLiteral(" ")) + w.text;
        }
        if (!text.isEmpty()) result.append({ box, text });
        line.clear();
    };
    do {
        if (ri->IsAtBeginningOf(tesseract::RIL_TEXTLINE)) flush();
        const char *raw = ri->GetUTF8Text(tesseract::RIL_WORD);
        if (!raw) continue;
        const QString text = QString::fromUtf8(raw).trimmed();
        delete[] raw;
        if (text.isEmpty()) continue;

        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (!ri->BoundingBox(tesseract::RIL_WORD, &x1, &y1, &x2, &y2)) continue;
        const QRectF bounds = QRectF(x1 / renderScale, y1 / renderScale,
                                     (x2 - x1) / renderScale, (y2 - y1) / renderScale)
                                  .intersected(pageRect);
        if (!bounds.isEmpty()) line.append({ bounds, text });
    } while (ri->Next(tesseract::RIL_WORD));
    flush();

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

QString OcrEngine::recognizeLine(const QImage &image, int dpi) const
{
#ifdef HAVE_TESSERACT
    if (!m_ready || image.isNull()) return {};
    auto *api = static_cast<tesseract::TessBaseAPI *>(m_api);
    const QImage img = image.convertToFormat(QImage::Format_RGB888);
    api->SetImage(img.bits(), img.width(), img.height(),
                  img.depth() / 8, static_cast<int>(img.bytesPerLine()));
    api->SetSourceResolution(dpi);
    api->SetPageSegMode(tesseract::PSM_SINGLE_LINE);
    char *raw = api->GetUTF8Text();
    api->Clear();
    api->SetPageSegMode(tesseract::PSM_AUTO);
    if (!raw) return {};
    const QString text = QString::fromUtf8(raw).simplified();
    delete[] raw;
    return text;
#else
    Q_UNUSED(image) Q_UNUSED(dpi)
    return {};
#endif
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
