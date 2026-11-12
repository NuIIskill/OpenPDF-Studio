#include "ui/edit/EditOpen.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/document/DocumentSource.hpp"

#include <QApplication>
#include <QEventLoop>
#include <QStringList>

namespace {

// A speck of dust OCR read as a line of its own, far smaller than the lines
// around, is no line; a text layer gives it a line's height.
bool dropSpecks(QList<TextLayout::OriginalLine> &lines)
{
    if (lines.size() < 2) return false;
    QList<double> heights;
    for (const TextLayout::OriginalLine &line : std::as_const(lines)) heights.append(line.rect.height());
    std::sort(heights.begin(), heights.end());
    const double low = heights.at(heights.size() / 2) * 0.4;
    return lines.removeIf([low](const TextLayout::OriginalLine &line) {
        return (line.rect.height() < low || line.rect.width() < low)
            && std::none_of(line.text.cbegin(), line.text.cend(), [](QChar c) { return c.isLetterOrNumber(); });
    }) > 0;
}

// Marks OCR reads from specks of dust: alone at either end of a line, or a
// quote or comma before its first word that nothing in the line closes.
bool dropSpeckMarks(QList<TextLayout::OriginalLine> &lines)
{
    static const QString marks = QStringLiteral(".,'`\u00b4\u201a|_~^\u00b0");
    const auto speck = [](const QString &word) {
        return std::all_of(word.cbegin(), word.cend(), [](QChar c) { return marks.contains(c); });
    };
    bool dropped = false;
    for (TextLayout::OriginalLine &line : lines) {
        QStringList words = line.text.split(u' ', Qt::SkipEmptyParts);
        if (words.isEmpty()) continue;
        const qsizetype was = words.size();
        while (words.size() > 1 && speck(words.last())) words.removeLast();
        while (words.size() > 1 && speck(words.first())) words.removeFirst();
        QString &first = words.first();
        const QString rest = words.mid(1).join(u' ');
        if (first.size() > 1 && QStringLiteral(",'`\u00b4\u201a").contains(first.front()) && first.at(1).isLetterOrNumber()
                && !rest.contains(u'\'') && !rest.contains(QChar(0x2018)) && !rest.contains(QChar(0x2019))
                && !first.mid(1).contains(u'\'') && !first.contains(QChar(0x2018)) && !first.contains(QChar(0x2019)))
            first.remove(0, 1);
        const QString text = words.join(u' ');
        if (words.size() == was && text == line.text.simplified()) continue;
        line.text = text;
        dropped = true;
    }
    return dropped;
}

// The step from one baseline to the next within a paragraph: the closest
// lines can be set, as the space between paragraphs only adds to it.
double usualStep(const QList<ScanFont::Fit> &fits)
{
    double least = 0.0;
    for (int i = 1; i < fits.size(); ++i) {
        const ScanFont::Fit &a = fits.at(i - 1), &b = fits.at(i);
        if (a.sizePt <= 0.0 || b.sizePt <= 0.0) continue;
        const double step = b.originPt.y() - a.originPt.y();
        if (step >= qMax(a.sizePt, b.sizePt) * 0.9 && (least <= 0.0 || step < least)) least = step;
    }
    return least;
}

// The lines around the clicked one set like it, in fits from the top: at
// its size and weight, covered about as well, and spaced alike.
std::pair<int, int> paragraphAround(const QList<ScanFont::Fit> &in, int at)
{
    const ScanFont::Fit clicked = in.at(at);
    const auto alike = [&](int i) {
        const ScanFont::Fit &m = in.at(i);
        if (m.sizePt <= 0.0 || clicked.sizePt <= 0.0) return true;
        // A clicked line far worse covered than the next one was misread:
        // its own size and weight say nothing, its spacing still does.
        if (clicked.score < m.score * 0.7) return true;
        // A weight told apart narrowly may be the scan's noise.
        constexpr double kClear = 0.01;
        const bool weightAlike = m.bold == clicked.bold || std::abs(m.boldMargin) < kClear
                              || std::abs(clicked.boldMargin) < kClear;
        return m.score >= clicked.score * 0.85
            && std::abs(m.sizePt - clicked.sizePt) <= clicked.sizePt * 0.04 && weightAlike;
    };
    // Lines set farther apart or closer than the others are another paragraph.
    const double usual = usualStep(in);
    const auto joined = [&](int i) {
        if (in.at(i).sizePt <= 0.0 || in.at(i + 1).sizePt <= 0.0) return true;
        return std::abs(in.at(i + 1).originPt.y() - in.at(i).originPt.y() - usual) <= usual * 0.1;
    };
    int first = at, last = at;
    while (first > 0 && alike(first - 1) && joined(first - 1)) --first;
    while (last + 1 < in.size() && alike(last + 1) && joined(last)) ++last;
    return { first, last };
}

// The text of some lines, one below the other, and the area they cover.
std::pair<QString, QRectF> joinedLines(const QList<TextLayout::OriginalLine> &lines)
{
    QStringList text;
    QRectF bounds;
    for (const TextLayout::OriginalLine &line : lines) {
        text.append(line.text);
        bounds = bounds.united(line.rect);
    }
    return { text.join(u'\n'), bounds };
}

}

QList<QRectF> EditOpener::scanLines(EditOpen &o) const
{
    // The lines of a scan taken, from the top: framed, or read from its text
    // layer or by OCR and narrowed to their own ink.
    QList<QRectF> lines;
    for (const TextLayout::OriginalLine &line : std::as_const(o.framedLines)) lines.append(line.rect);
    if (lines.isEmpty()) {
        QList<TextLayout::OriginalLine> read = scanTextLines(o);
        if (read.size() > 1 || read.value(0).rect != o.block.pdfBounds) {
            o.narrowToInk(read);
            for (const TextLayout::OriginalLine &line : std::as_const(read)) lines.append(line.rect);
        }
    }
    if (lines.isEmpty() && m_src->backend())
        lines = m_src->backend()->glyphRects(o.block.page, o.originalBounds, o.erasedZones);
    if (lines.isEmpty()) lines.append(o.block.pdfBounds);
    std::stable_sort(lines.begin(), lines.end(),
                     [](const QRectF &a, const QRectF &b) { return a.top() < b.top(); });
    return lines;
}

QList<TextLayout::OriginalLine> EditOpener::scanParagraph(EditOpen &o, bool &cleaned)
{
    // The lines of a scan taken, from the top, narrowed to their ink and
    // without what OCR read from specks of dust.
    QList<TextLayout::OriginalLine> texts = scanTextLines(o);
    cleaned = false;
    if (o.framedLines.isEmpty()) {
        o.narrowToInk(texts);
        cleaned = dropSpecks(texts);
        cleaned = dropSpeckMarks(texts) || cleaned;
    }
    std::stable_sort(texts.begin(), texts.end(), [](const TextLayout::OriginalLine &a,
                                                    const TextLayout::OriginalLine &b) {
        return a.rect.top() < b.rect.top();
    });
    return texts;
}

void EditOpener::matchScanFace(EditOpen &o)
{
    bool taken = false;
    QList<TextLayout::OriginalLine> texts = scanParagraph(o, taken);
    // The edit holds the lines taken.
    const auto takeTexts = [&]() {
        if (!taken) return;
        const auto [text, bounds] = joinedLines(texts);
        o.displayText = o.originalText = o.pdfText = o.block.text = text;
        o.block.pdfBounds = o.bounds = o.originalBounds = o.eraseBounds = bounds;
        o.paragraphBounds = false;
    };
    // A line alone, as a code in a form, is read again where the reading at hand fits its pixels
    // badly; what OCR read here was already set in the face of the scan.
    if (texts.size() == 1 && o.framedLines.isEmpty() && !m_src->backend()->hasScanText(o.block.page)) {
        const QString was = texts.first().text;
        rereadScanLine(o, texts.first());
        if (texts.first().text != was) {
            o.displayText = o.originalText = o.pdfText = o.block.text = texts.first().text;
        }
    }
    const auto lineOf = [&](int i) { return o.scanLine(texts.at(i).rect, texts.at(i).text); };
    const auto widest = [&]() {
        int w = 0;
        for (int i = 1; i < texts.size(); ++i)
            if (texts.at(i).rect.width() > texts.at(w).rect.width()) w = i;
        return w;
    };
    // The face is found on the widest line, with lines nearby when it is short.
    const auto faceOn = [&](int i) {
        return o.scanFont({ lineOf(i) }, scanContext(o, { texts.at(i) }));
    };
    const int wide = widest();
    ScanFont::Match face = faceOn(wide);
    // A line no face covers was misread, also by the OCR that set it in the scan's face.
    constexpr double kMisread = 0.4;
    if (texts.size() == 1 && o.framedLines.isEmpty() && face.isValid() && face.lines.value(0).score < kMisread
            && rereadScanLine(o, texts.first())) {
        taken = true;
        face = faceOn(wide);
    }
    if (!face.isValid()) {
        takeTexts();
        return;
    }

    // A scanned paragraph holds only the lines set like the one clicked: in
    // the face found, at its size and weight, and covered about as well.
    if (texts.size() > 1 && o.framedLines.isEmpty()) {
        int at = 0;
        for (int i = 1; i < texts.size(); ++i)
            if (distanceTo(texts.at(i).rect, o.clickPt) < distanceTo(texts.at(at).rect, o.clickPt)) at = i;
        const auto fitsIn = [&](const QString &family) {
            QList<ScanFont::Line> all;
            for (int i = 0; i < texts.size(); ++i) all.append(lineOf(i));
            const ScanFont::Match m = o.scanFont(all, {}, family);
            return m.lines.size() == texts.size() ? m.lines : QList<ScanFont::Fit>(texts.size());
        };
        QList<ScanFont::Fit> in = fitsIn(face.family);
        if (in.at(at).score < kMisread && rereadScanLine(o, texts[at])) {
            taken = true;
            in = fitsIn(face.family);
        }
        if (at != wide) {
            // The clicked line is set in another face than the widest one when
            // the face found on it covers it better, and well: a line OCR
            // misread fits no face.
            const ScanFont::Match own = faceOn(at);
            if (own.isValid() && own.family != face.family) {
                const QList<ScanFont::Fit> mine = fitsIn(own.family);
                if (mine.at(at).sizePt > 0.0 && mine.at(at).score >= kMisread
                        && (in.at(at).sizePt <= 0.0 || mine.at(at).score > in.at(at).score)) {
                    face = own;
                    in = mine;
                }
            }
        }
        const auto [first, last] = paragraphAround(in, at);
        if (first > 0 || last + 1 < texts.size()) {
            texts = texts.mid(first, last - first + 1);
            in = in.mid(first, last - first + 1);
            taken = true;
        }
        face.lines = in;
    }
    takeTexts();
    QList<double> sizes, spacings, inks;
    double bold = 0.0;
    for (const ScanFont::Fit &f : std::as_const(face.lines)) {
        if (f.sizePt <= 0.0) continue;
        sizes.append(f.sizePt);
        spacings.append(f.spacingPt);
        if (f.inkGray >= 0.0) inks.append(f.inkGray);
        bold += f.boldMargin;
    }
    if (sizes.isEmpty()) return;
    std::sort(sizes.begin(), sizes.end());
    face.sizePt = sizes.at(sizes.size() / 2);
    std::sort(inks.begin(), inks.end());
    face.inkGray = inks.isEmpty() ? -1.0 : inks.at(inks.size() / 2);
    face.bold = bold > 0.0;
    // Letters printed apart keep their spacing.
    std::sort(spacings.begin(), spacings.end());
    if (spacings.at(spacings.size() / 2) > 0.2) o.box.characterSpacingPt = spacings.at(spacings.size() / 2);
    o.scanFace = face;
    o.fontFamily = face.family;
    o.bold = face.bold;
    o.fontSizePt = face.sizePt;
}

bool EditOpener::rereadScanLine(EditOpen &o, TextLayout::OriginalLine &line)
{
    if (!m_ocr || !m_ocr->isReady()) return false;
    const auto score = [&](const QString &text) {
        const ScanFont::Match m = o.scanFont({ o.scanLine(line.rect, text) }, {});
        return m.isValid() && !m.lines.isEmpty() ? m.lines.first().score : -1.0;
    };
    // OCR misreads a line drawn far larger or smaller than it likes; it reads
    // it at a few heights, and the reading whose letters, in the face they fit
    // best, cover the scan best is taken.
    double best = score(line.text);
    // Letters that cover their pixels this well were read right.
    constexpr double kReadWell = 0.75;
    if (best >= kReadWell) return false;
    QString taken = line.text;
    QStringList tried { line.text.simplified() };
    for (const double inkPx : { 28.0, 40.0, 56.0 }) {
        int dpi = 0;
        const QImage pixels = o.lineImage(line.rect, inkPx, &dpi);
        if (pixels.isNull()) continue;
        const QString again = m_ocr->recognizeLine(pixels, dpi);
        if (again.isEmpty() || tried.contains(again)) continue;
        tried.append(again);
        const double s = score(again);
        constexpr double kBetter = 0.02;
        if (s > best + kBetter) { best = s; taken = again; }
    }
    const bool changed = taken != line.text;
    line.text = taken;
    return changed;
}

QList<ScanFont::Line> EditOpener::scanContext(EditOpen &o, const QList<TextLayout::OriginalLine> &taken)
{
    // The nearest other lines of the page.
    QList<std::pair<QRectF, QString>> near;
    const QRectF around = o.block.pdfBounds.adjusted(-o.pageSize.width(), -80.0, o.pageSize.width(), 80.0);
    if (m_src->backend()) {
        QList<TextLayout::OriginalLine> lines = m_src->backend()->originalLines(o.block.page, around, o.erasedZones);
        o.narrowToInk(lines);
        for (const TextLayout::OriginalLine &line : std::as_const(lines)) near.append({ line.rect, line.text });
    }
    const QPointF at = o.block.pdfBounds.center();
    std::stable_sort(near.begin(), near.end(), [&](const auto &a, const auto &b) {
        return distanceTo(a.first, at) < distanceTo(b.first, at);
    });
    QList<ScanFont::Line> out;
    for (const auto &[rect, text] : std::as_const(near)) {
        bool own = false;
        for (const TextLayout::OriginalLine &t : taken) own |= t.rect.intersects(rect);
        if (own || text.trimmed().isEmpty()) continue;
        out.append(o.scanLine(rect, text));
        if (out.size() >= 8) break;
    }
    return out;
}

void EditOpener::readScansNear(int page)
{
    // The scans in view are read before the text tool clicks them, the one shown first.
    PdfBackend *backend = m_src->backend();
    if (!backend || !m_src->worker() || !m_ocr || !m_ocr->isReady()) return;
    for (const int near : { page + 1, page - 1, page }) {
        if (near < 0 || near >= m_src->pageCount() || m_scansAsked.contains(near)) continue;
        m_scansAsked.insert(near);
        if (!backend->hasSelectableText(near) && !backend->hasScanText(near))
            m_src->worker()->readScan(near);
    }
}

bool EditOpener::waitForScan(int page)
{
    PdfBackend *backend = m_src->backend();
    DocumentWorker *worker = m_src->worker();
    if (!backend || !worker) return false;
    if (backend->hasScanText(page)) return true;
    m_scansAsked.insert(page);
    QEventLoop wait;
    const QMetaObject::Connection read = QObject::connect(worker, &DocumentWorker::scanTextRead,
                                                          &wait, [&wait, page](int p) {
        if (p == page) wait.quit();
    });
    const QMetaObject::Connection stopped = QObject::connect(worker, &DocumentWorker::stopped,
                                                             &wait, &QEventLoop::quit);
    worker->readScan(page);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    wait.exec(QEventLoop::ExcludeUserInputEvents);
    QApplication::restoreOverrideCursor();
    QObject::disconnect(read);
    QObject::disconnect(stopped);
    return backend->hasScanText(page);
}

QList<TextLayout::OriginalLine> EditOpener::scanTextLines(const EditOpen &o) const
{
    // The lines of a scan taken with their text, from its text layer or as OCR read it.
    QList<TextLayout::OriginalLine> lines = o.framedLines;
    if (lines.isEmpty() && m_src->backend()) {
        const QRectF within = o.originalBounds.adjusted(-1.0, -1.0, 1.0, 1.0);
        for (const TextLayout::OriginalLine &line :
             m_src->backend()->originalLines(o.block.page, o.originalBounds, o.erasedZones))
            if (within.contains(line.rect.center())) lines.append(line);
    }
    if (lines.isEmpty()) {
        TextLayout::OriginalLine line;
        line.rect = o.block.pdfBounds;
        line.text = o.displayText;
        lines.append(line);
    }
    return lines;
}

void EditOpener::measureScan(EditOpen &o)
{
    // The scanned ink of each line taken is painted over with the paper.
    if (!o.scanned || o.isSessionEdit) return;
    const QList<QRectF> lines = scanLines(o);
    o.scanRects.clear();
    for (const QRectF &line : std::as_const(lines)) {
        // Lines read are already their ink; a frame takes its characters only, not the words beside them.
        QRectF ink = line;
        if (!o.framedLines.isEmpty()) {
            ink = o.inkAround(line);
            ink.setLeft(qMax(ink.left(), line.left() - 0.5));
            ink.setRight(qMin(ink.right(), line.right() + 0.5));
        }
        o.scanRects.append(ink.adjusted(-0.75, -0.75, 0.75, 0.75));
    }
}

#endif
