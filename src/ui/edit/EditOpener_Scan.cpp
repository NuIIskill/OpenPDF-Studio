#include "ui/edit/EditOpen.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/document/DocumentSource.hpp"

#include <QStringList>

QList<QRectF> EditOpener::scanLines(const EditOpen &o) const
{
    // The lines of a scan taken, from the top: framed, read from its text layer or by OCR.
    QList<QRectF> lines;
    for (const TextLayout::OriginalLine &line : std::as_const(o.framedLines)) lines.append(line.rect);
    if (lines.isEmpty() && m_src->backend())
        lines = m_src->backend()->glyphRects(o.block.page, o.originalBounds, o.erasedZones);
    if (lines.isEmpty()) lines.append(o.block.pdfBounds);
    std::stable_sort(lines.begin(), lines.end(),
                     [](const QRectF &a, const QRectF &b) { return a.top() < b.top(); });
    return lines;
}

void EditOpener::matchScanFace(EditOpen &o)
{
    QList<TextLayout::OriginalLine> texts = scanTextLines(o);
    std::stable_sort(texts.begin(), texts.end(), [](const TextLayout::OriginalLine &a,
                                                    const TextLayout::OriginalLine &b) {
        return a.rect.top() < b.rect.top();
    });
    // A line alone, as a code in a form, is read again where the reading at hand fits its pixels badly.
    if (texts.size() == 1 && o.framedLines.isEmpty()) {
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
    ScanFont::Match face = faceOn(widest());
    if (!face.isValid()) return;

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
        const ScanFont::Fit reference = face.lines.value(0);
        QList<ScanFont::Fit> in = fitsIn(face.family);
        if (in.at(at).sizePt <= 0.0 || in.at(at).score < reference.score * 0.85) {
            // The clicked line is set in another face than the widest one.
            face = faceOn(at);
            if (!face.isValid()) return;
            in = fitsIn(face.family);
        }
        const ScanFont::Fit clicked = in.at(at);
        const auto alike = [&](int i) {
            const ScanFont::Fit &m = in.at(i);
            if (m.sizePt <= 0.0 || clicked.sizePt <= 0.0) return true;
            return m.score >= clicked.score * 0.85
                && std::abs(m.sizePt - clicked.sizePt) <= clicked.sizePt * 0.04 && m.bold == clicked.bold;
        };
        int first = at, last = at;
        while (first > 0 && alike(first - 1)) --first;
        while (last + 1 < texts.size() && alike(last + 1)) ++last;
        if (first > 0 || last + 1 < texts.size()) {
            texts = texts.mid(first, last - first + 1);
            in = in.mid(first, last - first + 1);
            QStringList text;
            QRectF bounds;
            for (const TextLayout::OriginalLine &line : std::as_const(texts)) {
                text.append(line.text);
                bounds = bounds.united(line.rect);
            }
            o.displayText = o.originalText = o.pdfText = text.join(u'\n');
            o.block.text = o.displayText;
            o.block.pdfBounds = o.bounds = o.originalBounds = o.eraseBounds = bounds;
            o.paragraphBounds = false;
        }
        face.lines = in;
    }
    QList<double> sizes, spacings;
    int bold = 0;
    for (const ScanFont::Fit &f : std::as_const(face.lines)) {
        if (f.sizePt <= 0.0) continue;
        sizes.append(f.sizePt);
        spacings.append(f.spacingPt);
        bold += f.bold ? 1 : -1;
    }
    if (sizes.isEmpty()) return;
    std::sort(sizes.begin(), sizes.end());
    face.sizePt = sizes.at(sizes.size() / 2);
    face.bold = bold > 0;
    // Letters printed apart keep their spacing.
    std::sort(spacings.begin(), spacings.end());
    if (spacings.at(spacings.size() / 2) > 0.2) o.box.characterSpacingPt = spacings.at(spacings.size() / 2);
    o.scanFace = face;
    o.fontFamily = face.family;
    o.bold = face.bold;
    o.fontSizePt = face.sizePt;
}

void EditOpener::rereadScanLine(EditOpen &o, TextLayout::OriginalLine &line)
{
    if (!m_ocr || !m_ocr->isReady()) return;
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
    if (best >= kReadWell) return;
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
    line.text = taken;
}

QList<ScanFont::Line> EditOpener::scanContext(EditOpen &o, const QList<TextLayout::OriginalLine> &taken)
{
    // The nearest other lines of the page.
    QList<std::pair<QRectF, QString>> near;
    const QRectF around = o.block.pdfBounds.adjusted(-o.pageSize.width(), -80.0, o.pageSize.width(), 80.0);
    if (m_ocrCache.contains(o.block.page)) {
        for (const OcrEngine::Block &b : std::as_const(m_ocrCache[o.block.page]))
            if (around.intersects(b.pdfBounds)) near.append({ b.pdfBounds, b.text });
    } else if (m_src->backend()) {
        for (const TextLayout::OriginalLine &line :
             m_src->backend()->originalLines(o.block.page, around, o.erasedZones))
            near.append({ line.rect, line.text });
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
        // A frame takes its characters only, not the words beside them.
        QRectF ink = o.inkAround(line);
        if (!o.framedLines.isEmpty()) {
            ink.setLeft(qMax(ink.left(), line.left() - 0.5));
            ink.setRight(qMin(ink.right(), line.right() + 0.5));
        }
        o.scanRects.append(ink.adjusted(-0.75, -0.75, 0.75, 0.75));
    }
}

#endif
