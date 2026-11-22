#include "engine/ocr/OcrEngine.hpp"
#include "engine/edit/ScanPen.hpp"
#include "engine/edit/ScanRules.hpp"
#include "engine/ocr/OcrLanguages.hpp"
#include "engine/ocr/OcrMissed.hpp"
#include "engine/ocr/OcrStyle.hpp"

#ifdef HAVE_TESSERACT
#  include <tesseract/baseapi.h>
#  include <tesseract/ocrclass.h>
#  include <leptonica/allheaders.h>
#endif

#include <QDebug>
#include <QStringList>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// Words set close, in small print or before a sign, OCR reads as one: gaps
// between characters as wide as a space and far wider than all others in the
// word part it.
void splitAtSpaces(OcrEngine::Line &line)
{
    QList<double> heights;
    for (const OcrEngine::Word &w : std::as_const(line))
        for (const QRectF &c : w.chars)
            if (!c.isEmpty()) heights.append(c.height());
    if (heights.size() < 4) return;
    std::sort(heights.begin(), heights.end());
    const double tall = heights.at(heights.size() * 3 / 4);
    OcrEngine::Line out;
    for (const OcrEngine::Word &w : std::as_const(line)) {
        // The gaps between the characters, each at the index of the one after it.
        QList<std::pair<qsizetype, double>> gaps;
        for (qsizetype c = 1; c < w.chars.size(); ++c)
            if (w.chars.at(c) != w.chars.at(c - 1) && !w.chars.at(c).isEmpty() && !w.chars.at(c - 1).isEmpty())
                gaps.append({ c, w.chars.at(c).left() - w.chars.at(c - 1).right() });
        QList<double> widths;
        for (const auto &gap : std::as_const(gaps)) widths.append(gap.second);
        std::sort(widths.begin(), widths.end());
        double space = -1.0;
        for (qsizetype k = 0; k + 1 < widths.size(); ++k)
            if (widths.at(k + 1) >= tall * 0.3 && widths.at(k + 1) >= qMax(1.0, widths.at(k)) * 1.8) {
                space = widths.at(k + 1);
                break;
            }
        qsizetype from = 0;
        const auto part = [&](qsizetype to) {
            OcrEngine::Word piece = w;
            piece.text = w.text.mid(from, to - from);
            piece.chars = w.chars.mid(from, to - from);
            piece.pdfBounds = QRectF();
            for (const QRectF &c : std::as_const(piece.chars))
                if (!c.isEmpty()) piece.pdfBounds = piece.pdfBounds.isNull() ? c : piece.pdfBounds.united(c);
            out.append(piece);
            from = to;
        };
        for (const auto &[c, gap] : std::as_const(gaps))
            if (space > 0.0 && gap >= space) part(c);
        part(w.text.size());
    }
    line = out;
}

// A line OCR read over table cells is parted where its words stand far
// apart: a gap wider than five times the line's height. Narrower gaps, as of
// a justified narrow column or between columns, are told apart where the
// text is laid out, by the lines around.
void splitAtGaps(QList<OcrEngine::Line> &lines)
{
    QList<OcrEngine::Line> out;
    for (const OcrEngine::Line &line : std::as_const(lines)) {
        double height = 0.0;
        for (const OcrEngine::Word &w : line) height = std::max(height, w.pdfBounds.height());
        OcrEngine::Line part;
        for (int i = 0; i < line.size(); ++i) {
            if (i > 0 && line.at(i).pdfBounds.left() - line.at(i - 1).pdfBounds.right() > height * 5.0) {
                out.append(part);
                part.clear();
            }
            part.append(line.at(i));
        }
        if (!part.isEmpty()) out.append(part);
    }
    lines = out;
}

}

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
        // Sauvola thresholds each part of a page by its surroundings, so a
        // shadow or a grey scan keeps its text.
        api->SetVariable("thresholding_method", "2");
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

    // A line is split where its words stand far apart, as table cells do: a
    // gap wider than two and a half times the line's height.
    for (const Line &line : recognizeLines(pageImage, pageSizePts, renderScale)) {
        double height = 0.0;
        for (const Word &w : line) height = std::max(height, w.pdfBounds.height());
        QRectF box;
        QString text;
        for (int i = 0; i < line.size(); ++i) {
            const Word &w = line.at(i);
            if (i > 0 && w.pdfBounds.left() - line.at(i - 1).pdfBounds.right() > height * 2.5) {
                if (!text.isEmpty()) result.append({ box, text });
                box = QRectF();
                text.clear();
            }
            box = box.isNull() ? w.pdfBounds : box.united(w.pdfBounds);
            text += (text.isEmpty() ? QString() : QStringLiteral(" ")) + w.text;
        }
        if (!text.isEmpty()) result.append({ box, text });
    }
    qDebug() << "[OCR] Page recognition complete:" << result.size() << "lines found";
    return result;
}

#ifdef HAVE_TESSERACT
// A page without its form's rules.
static QImage withoutRules(const QImage &gray, qreal pxPerPt)
{
    if (gray.format() != QImage::Format_Grayscale8) return gray;
    int tones[256] = {}, paper = 255;
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) ++tones[row[x]];
    }
    for (int t = 0, most = 0; t < 256; ++t)
        if (tones[t] > most) { most = tones[t]; paper = t; }
    QImage out = gray;
    const auto clear = [&](const QRect &area) {
        const QRect r = area.intersected(out.rect());
        for (int y = r.top(); y <= r.bottom(); ++y) std::fill_n(out.scanLine(y) + r.left(), r.width(), uchar(paper));
    };
    for (const QRect &rule : ScanRules::find(gray, pxPerPt))
        clear(rule.width() > rule.height() ? rule.adjusted(0, -1, 0, 1) : rule.adjusted(-1, 0, 1, 0));
    // Nor is a long curve drawn by hand text: read with the page it pulls the lines beside it into one.
    return ScanPen::withoutCurves(out, pxPerPt);
}
#endif

void OcrEngine::readMissedRows(QList<Line> &lines, const QImage &straight, qreal renderScale, int &paragraph,
                               const std::function<bool()> &cancelled) const
{
    // Rows of letters the page's reading left out, as figures printed far apart, are read set close:
    // a row is taken where that reading has a letter or figure for each of its letters. What was read
    // upright through a column of such rows goes first.
    QList<QRectF> read;
    QList<std::pair<qsizetype, qsizetype>> from;
    for (qsizetype l = 0; l < lines.size(); ++l)
        for (qsizetype k = 0; k < lines.at(l).size(); ++k) {
            read.append(lines.at(l).at(k).pdfBounds);
            from.append({ l, k });
        }
    QList<int> upright;
    const QList<OcrMissed::Row> missed = OcrMissed::rows(straight, read, renderScale, &upright);
    for (qsizetype i = upright.size() - 1; i >= 0; --i) lines[from.at(upright.at(i)).first].removeAt(from.at(upright.at(i)).second);
    lines.removeIf([](const Line &l) { return l.isEmpty(); });
    for (const OcrMissed::Row &row : missed) {
        if (cancelled && cancelled()) break;
        // A row read before as one line with a letter or figure for each of its letters was read well: its
        // text stands, and its letters where the scan has them, which a word read over letters far apart tells badly.
        QString text, pieces;
        if (row.over) {
            qsizetype in = 0;
            QList<std::pair<qreal, QString>> had;
            for (const Line &l : std::as_const(lines)) {
                bool here = false;
                for (const Word &w : l) {
                    if (!row.bounds.adjusted(-2, -2, 2, 2).contains(w.pdfBounds.center().toPoint())) continue;
                    had.append({ w.pdfBounds.left(), w.text });
                    here = true;
                }
                in += here;
            }
            std::sort(had.begin(), had.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            for (const auto &[left, word] : std::as_const(had)) text += word;
            text.remove(u' ');
            if (text.size() != row.letters.size()) text.clear();
            if (in != 1) std::swap(text, pieces);
        }
        if (text.isEmpty()) {
            text = recognizeLine(OcrMissed::closedUp(straight, row), qRound(renderScale * 72.0));
            text.remove(u' ');
            // Where the row set close reads as no row, what its pieces were read as stands, a letter for each.
            if (text.size() != row.letters.size() && !pieces.isEmpty()) text = pieces;
        }
        if (text.size() != row.letters.size() || !std::all_of(text.cbegin(), text.cend(), [](QChar c) { return c.isLetterOrNumber(); })) continue;
        // What was read of such a row before, in pieces, gives way to the row.
        if (row.over) {
            for (Line &l : lines)
                l.removeIf([&](const Word &w) { return row.bounds.adjusted(-2, -2, 2, 2).contains(w.pdfBounds.center().toPoint()); });
            lines.removeIf([](const Line &l) { return l.isEmpty(); });
        }
        Line found;
        ++paragraph;
        for (const auto &[first, last] : row.words()) {
            Word word { {}, text.mid(first, last - first + 1), {}, qreal(row.bounds.bottom() + 1), paragraph };
            for (int i = first; i <= last; ++i) {
                word.chars.append(QRectF(row.letters.at(i)));
                word.pdfBounds |= word.chars.last();
            }
            found.append(word);
        }
        lines.append(found);
    }
}

QList<OcrEngine::Line> OcrEngine::recognizeLines(const QImage &pageImage,
                                                 const QSizeF  &pageSizePts,
                                                 qreal          renderScale,
                                                 bool           settle,
                                                 const std::function<bool()> &cancelled,
                                                 const std::function<void(int percent)> &progress) const
{
    QList<Line> lines;

#ifdef HAVE_TESSERACT
    if (!m_ready || pageImage.isNull() || renderScale <= 0.0) return lines;

    auto *api = static_cast<tesseract::TessBaseAPI *>(m_api);
    QImage straight;
    const QTransform toPage = setPage(pageImage, renderScale, &straight);
    api->SetPageSegMode(tesseract::PSM_AUTO);

    // Reading is most of the time, setting the words in the scan's face the rest.
    constexpr int kReading = 60;
    int shown = -1;
    const auto report = [&](int percent) {
        if (!progress || percent == shown) return;
        shown = percent;
        progress(percent);
    };
    tesseract::ETEXT_DESC monitor;
    struct Watch {
        const std::function<bool()> &cancelled;
        const std::function<void(int)> &report;
        const tesseract::ETEXT_DESC &monitor;
    };
    const std::function<void(int)> reported = report;
    Watch watch { cancelled, reported, monitor };
    // Tesseract asks whether to stop after each word, with its progress set.
    monitor.cancel = [](void *data, int) {
        const auto *w = static_cast<const Watch *>(data);
        w->report(w->monitor.progress * kReading / 100);
        return w->cancelled && w->cancelled();
    };
    monitor.cancel_this = &watch;
    if (api->Recognize(cancelled || progress ? &monitor : nullptr) != 0 || (cancelled && cancelled())) {
        if (!cancelled || !cancelled()) qWarning() << "[OCR] Recognize() failed";
        api->Clear();
        return lines;
    }

    tesseract::ResultIterator *ri = api->GetIterator();
    if (!ri) { api->Clear(); return lines; }

    const auto bounds = [&](tesseract::PageIteratorLevel level) {
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (!ri->BoundingBox(level, &x1, &y1, &x2, &y2)) return QRectF();
        return QRectF(x1, y1, x2 - x1, y2 - y1);
    };

    Line line;
    Word word;
    bool skip = false;
    int paragraph = 0;
    const auto endWord = [&]() {
        if (!word.text.trimmed().isEmpty()) line.append(word);
        word = Word();
    };
    do {
        if (ri->Empty(tesseract::RIL_SYMBOL)) continue;
        if (ri->IsAtBeginningOf(tesseract::RIL_WORD)) {
            endWord();
            if (ri->IsAtBeginningOf(tesseract::RIL_TEXTLINE) && !line.isEmpty()) {
                lines.append(line);
                line.clear();
            }
            if (ri->IsAtBeginningOf(tesseract::RIL_PARA)) ++paragraph;
            word.pdfBounds = bounds(tesseract::RIL_WORD);
            word.paragraph = paragraph;
            skip = word.pdfBounds.isEmpty();
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            const double mid = word.pdfBounds.center().x();
            word.baseline = ri->Baseline(tesseract::RIL_TEXTLINE, &x1, &y1, &x2, &y2)
                ? (x2 != x1 ? y1 + (y2 - y1) * (mid - x1) / double(x2 - x1) : double(y1))
                : word.pdfBounds.bottom();
        }
        if (skip) continue;
        const char *raw = ri->GetUTF8Text(tesseract::RIL_SYMBOL);
        if (!raw) continue;
        const QString symbol = QString::fromUtf8(raw);
        delete[] raw;
        const QRectF box = bounds(tesseract::RIL_SYMBOL);
        for (const QChar c : symbol) {
            word.text += c;
            word.chars.append(box);
        }
    } while (ri->Next(tesseract::RIL_SYMBOL));
    endWord();
    if (!line.isEmpty()) lines.append(line);

    delete ri;
    api->Clear();

    readMissedRows(lines, straight, renderScale, paragraph, cancelled);

    for (Line &l : lines) splitAtSpaces(l);
    splitAtGaps(lines);
    // A speck of dust beside a line is read as a mark of its own, far smaller than the words: it is no word.
    for (Line &l : lines) {
        if (l.size() < 2) continue;
        QList<double> heights;
        for (const Word &w : std::as_const(l)) heights.append(w.pdfBounds.height());
        std::sort(heights.begin(), heights.end());
        const double small = heights.at(heights.size() / 2) * 0.25;
        const auto speck = [small](const Word &w) {
            return w.pdfBounds.width() < small && w.pdfBounds.height() < small
                && std::none_of(w.text.cbegin(), w.text.cend(), [](QChar c) { return c.isLetterOrNumber(); });
        };
        while (l.size() > 1 && speck(l.last())) l.removeLast();
        while (l.size() > 1 && speck(l.first())) l.removeFirst();
    }

    report(kReading);
    if (settle)
        OcrStyle::settle(straight, renderScale, lines, cancelled, [&](qreal done) {
            report(kReading + int(done * (100 - kReading)));
        });
    if (cancelled && cancelled()) return {};

    // What was found on the straightened pixels is turned back onto the page.
    const QRectF pageRect(QPointF(0, 0), pageSizePts);
    for (Line &l : lines) {
        for (Word &w : l) {
            w.baseline = toPage.map(QPointF(w.pdfBounds.center().x(), w.baseline)).y();
            w.pdfBounds = toPage.mapRect(w.pdfBounds).intersected(pageRect);
            for (QRectF &c : w.chars)
                if (!c.isEmpty()) c = toPage.mapRect(c).intersected(pageRect);
        }
        l.erase(std::remove_if(l.begin(), l.end(), [](const Word &w) { return w.pdfBounds.isEmpty(); }), l.end());
    }
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const Line &l) { return l.isEmpty(); }), lines.end());
#else
    Q_UNUSED(pageImage)
    Q_UNUSED(pageSizePts)
    Q_UNUSED(renderScale)
    Q_UNUSED(settle)
    Q_UNUSED(cancelled)
    Q_UNUSED(progress)
#endif

    return lines;
}

QTransform OcrEngine::setPage(const QImage &pageImage, qreal renderScale, QImage *straight) const
{
    QTransform toPage = QTransform::fromScale(1.0 / renderScale, 1.0 / renderScale);
#ifdef HAVE_TESSERACT
    auto *api = static_cast<tesseract::TessBaseAPI *>(m_api);
    const QImage gray = pageImage.convertToFormat(QImage::Format_Grayscale8);
    Pix *pix = pixCreate(gray.width(), gray.height(), 8);
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *from = gray.constScanLine(y);
        l_uint32 *to = pixGetData(pix) + y * pixGetWpl(pix);
        for (int x = 0; x < gray.width(); ++x) SET_DATA_BYTE(to, x, from[x]);
    }

    // A scan lies a little askew; Tesseract reads it straightened, and what
    // it finds is turned back onto the page.
    l_float32 angle = 0.0f, confidence = 0.0f;
    Pix *ink = nullptr;
    if (pixSauvolaBinarizeTiled(pix, 25, 0.35f, 1, 1, nullptr, &ink) == 0)
        pixFindSkew(ink, &angle, &confidence);
    pixDestroy(&ink);
    if (confidence >= 3.0f && std::abs(angle) >= 0.1f) {
        if (Pix *straight = pixRotate(pix, qDegreesToRadians(angle), L_ROTATE_AREA_MAP,
                                      L_BRING_IN_WHITE, 0, 0)) {
            pixDestroy(&pix);
            pix = straight;
            const QPointF center(gray.width() / 2.0, gray.height() / 2.0);
            toPage.translate(center.x(), center.y());
            toPage.rotate(-angle);
            toPage.translate(-center.x(), -center.y());
        }
    }

    // A form's rules are no text: read with them, a dashed rule comes back as words and a row between two
    // of them as its neighbour. They are found on the straightened page, where they run level and whole.
    QImage level(int(pixGetWidth(pix)), int(pixGetHeight(pix)), QImage::Format_Grayscale8);
    for (int y = 0; y < level.height(); ++y) {
        const l_uint32 *from = pixGetData(pix) + y * pixGetWpl(pix);
        uchar *to = level.scanLine(y);
        for (int x = 0; x < level.width(); ++x) to[x] = uchar(GET_DATA_BYTE(from, x));
    }
    if (const QImage plain = withoutRules(level, renderScale); plain.constBits() != level.constBits()) {
        level = plain;
        for (int y = 0; y < level.height(); ++y) {
            const uchar *from = level.constScanLine(y);
            l_uint32 *to = pixGetData(pix) + y * pixGetWpl(pix);
            for (int x = 0; x < level.width(); ++x) SET_DATA_BYTE(to, x, from[x]);
        }
    }
    if (straight) *straight = level;
    api->SetImage(pix);
    api->SetSourceResolution(qRound(renderScale * 72.0));
    pixDestroy(&pix);
#else
    Q_UNUSED(pageImage)
    Q_UNUSED(straight)
#endif
    return toPage;
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
    // A line cut from a page is evenly lit and keeps one threshold for all of it.
    api->SetVariable("thresholding_method", "0");
    char *raw = api->GetUTF8Text();
    api->SetVariable("thresholding_method", "2");
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
