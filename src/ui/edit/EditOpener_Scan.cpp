#include "ui/edit/EditOpen.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/document/DocumentSource.hpp"
#include "ui/edit/ScanCleanup.hpp"

#include <QApplication>
#include <QEventLoop>
#include <QFontMetricsF>
#include <QStringList>
#include <QTransform>
#include <QtMath>

#include <functional>

namespace {

// Letters that cover their pixels this well were read right, in the face they are printed in;
// a face the document was found to use before is the line's where it covers it this well.
constexpr double kReadWell = 0.75, kKnownFace = 0.6;

// A line no face covers better than this was misread.
constexpr double kMisread = 0.4;

// Where the text layer starts each word of a line, which helps tell the words apart in its pixels.
QList<double> wordStarts(const TextLayout::OriginalLine &line)
{
    QList<double> starts;
    for (qsizetype c = 0; line.charX.size() == line.text.size() && c < line.text.size(); ++c)
        if (line.text.at(c) != u' ' && (c == 0 || line.text.at(c - 1) == u' ')) starts.append(line.charX.at(c));
    return starts;
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
std::pair<int, int> paragraphAround(const QList<ScanFont::Fit> &in, const QList<double> &strokes,
                                    const QList<QRectF> &rects, const QList<bool> &ruled, int at)
{
    const ScanFont::Fit clicked = in.at(at);
    const auto alike = [&](int i) {
        const ScanFont::Fit &m = in.at(i);
        // Lines justified alike are one paragraph, also where one of them is mostly bold.
        if (rects.size() == in.size() && m.words.size() >= 3 && clicked.words.size() >= 3
                && std::abs(rects.at(i).left() - rects.at(at).left()) <= 1.5
                && std::abs(rects.at(i).right() - rects.at(at).right()) <= 1.5
                && std::abs(m.sizePt - clicked.sizePt) <= clicked.sizePt * 0.08)
            return true;
        // Strokes far thicker or thinner are another weight, however the text
        // was read; a line with a few bold words is thicker only a little.
        const double a = strokes.value(i), b = strokes.value(at);
        const double thicker = a > 0.0 && b > 0.0 ? qMax(a, b) / qMin(a, b) : 1.0;
        if (thicker > 1.5 || (thicker > 1.25 && m.bold != clicked.bold)) return false;
        if (m.sizePt <= 0.0 || clicked.sizePt <= 0.0) return true;
        // A line far worse covered than the other was misread: its own size
        // and weight say little, its spacing still does. Noise and misread
        // words make lines of one paragraph covered unevenly anyway.
        if (clicked.score < m.score * 0.7 || m.score < clicked.score * 0.7)
            return std::abs(m.sizePt - clicked.sizePt) <= clicked.sizePt * 0.08;
        // A weight told apart narrowly may be the scan's noise, and the size
        // is then measured in the other weight's widths.
        constexpr double kClear = 0.01, kNarrow = 0.1;
        const bool weightAlike = m.bold == clicked.bold || std::abs(m.boldMargin) < kClear
                              || std::abs(clicked.boldMargin) < kClear;
        const bool narrowly = m.bold != clicked.bold
                           && qMin(std::abs(m.boldMargin), std::abs(clicked.boldMargin)) < kNarrow;
        // A line of a few words measures its size less closely.
        const bool few = m.words.size() <= 3 || clicked.words.size() <= 3;
        const double tolerance = narrowly ? 0.13 : few ? 0.08 : 0.04;
        return std::abs(m.sizePt - clicked.sizePt) <= clicked.sizePt * tolerance && (weightAlike || narrowly);
    };
    // Lines set farther apart or closer than the others are another paragraph, as are those a rule parts.
    const double usual = usualStep(in);
    const auto joined = [&](int i) {
        if (ruled.value(i)) return false;
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
        if (read.size() > 1 || !read.value(0).charX.isEmpty() || read.value(0).rect != o.block.pdfBounds) {
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
        // The lines around on the page, as wide as it, tell columns apart.
        const double step = texts.isEmpty() ? 0.0 : texts.first().rect.height() * 3.0;
        const QList<TextLayout::OriginalLine> around = m_src->backend()
            ? m_src->backend()->originalLines(o.block.page, QRectF(0.0, o.originalBounds.top() - step, o.pageSize.width(),
                                                                   o.originalBounds.height() + 2.0 * step), o.erasedZones)
            : QList<TextLayout::OriginalLine>();
        // Lines a form's rule parts from the clicked one are none of its paragraph: what tells columns
        // apart looks at its own lines alone.
        if (texts.size() > 1) {
            std::stable_sort(texts.begin(), texts.end(), [](const TextLayout::OriginalLine &a, const TextLayout::OriginalLine &b) {
                return a.rect.top() < b.rect.top();
            });
            qsizetype at = 0;
            const auto off = [&o](const QRectF &r) {
                return std::hypot(qMax(0.0, qMax(r.left() - o.clickPt.x(), o.clickPt.x() - r.right())),
                                  qMax(0.0, qMax(r.top() - o.clickPt.y(), o.clickPt.y() - r.bottom())) * 4.0);
            };
            for (qsizetype i = 1; i < texts.size(); ++i)
                if (off(texts.at(i).rect) < off(texts.at(at).rect)) at = i;
            qsizetype first = at, last = at;
            while (first > 0 && !o.ruledBetween(texts.at(first - 1).rect, texts.at(first).rect)) --first;
            while (last + 1 < texts.size() && !o.ruledBetween(texts.at(last).rect, texts.at(last + 1).rect)) ++last;
            if (first > 0 || last + 1 < texts.size()) {
                texts = texts.mid(first, last - first + 1);
                cleaned = true;
            }
        }
        cleaned = ScanCleanup::keepColumn(texts, around, o.clickPt.x()) || cleaned;
        cleaned = ScanCleanup::keepSide(texts, o.clickPt.x()) || cleaned;
        cleaned = ScanCleanup::joinPieces(texts, o.contentItem.isValid() ? o.contentItem.bounds : QRectF()) || cleaned;
        // On a page lying askew a line's box stands upright around it, and the line is measured along its slope.
        QRectF all;
        for (const TextLayout::OriginalLine &line : std::as_const(texts)) all = all.united(line.rect);
        const double slope = texts.isEmpty() ? 0.0 : o.inkSlope(all);
        // Marks are read before specks are dropped, as OCR may read a bullet as one; capitals and
        // ascenders reach about three quarters of the size.
        cleaned = ScanCleanup::readMarks(texts, [&texts, slope](int i, double x) {
            const TextLayout::OriginalLine &line = texts.at(i);
            const double base = line.baseline + slope * (x - line.charX.value(0, line.rect.left()));
            const double top = line.rect.top() + (slope > 0.0 ? slope * (x - line.rect.left()) : slope * (x - line.rect.right()));
            return std::make_pair((base - top) / 0.75, base);
        }, [&o](const QRectF &pt) { return o.blob(pt); }) || cleaned;
        cleaned = ScanCleanup::dropSpecks(texts) || cleaned;
        cleaned = ScanCleanup::dropSpeckMarks(texts) || cleaned;
    }
    std::stable_sort(texts.begin(), texts.end(), [](const TextLayout::OriginalLine &a,
                                                    const TextLayout::OriginalLine &b) {
        return a.rect.top() < b.rect.top();
    });
    return texts;
}

ScanFont::Line EditOpener::scanLineOf(EditOpen &o, const TextLayout::OriginalLine &line)
{
    return o.scanLine(line.rect, line.text, wordStarts(line), line.charX.isEmpty() ? -1.0 : line.baseline);
}

ScanFont::Match EditOpener::scanFaceOn(EditOpen &o, const TextLayout::OriginalLine &line)
{
    // The face is found on a line with lines nearby when it is short. One found in this
    // document before is tried first: where it covers the line well, it is the line's too.
    // Not for a few figures, which most faces cover well: they are told with the rows around them.
    const QList<ScanFont::Line> context = scanContext(o, { line });
    const qsizetype digits = std::count_if(line.text.cbegin(), line.text.cend(), [](QChar c) { return c.isDigit(); });
    const qsizetype alphas = std::count_if(line.text.cbegin(), line.text.cend(), [](QChar c) { return c.isLetter(); });
    const bool figures = line.text.size() < 16 && (alphas == 0 || (digits >= 3 && alphas * 2 <= digits));
    for (const QString &family : std::as_const(m_faces.families)) {
        if (figures) break;
        const ScanFont::Match known = o.scanFont({ scanLineOf(o, line) }, context, family);
        if (known.isValid() && known.lines.value(0).score >= kKnownFace) return known;
    }
    return o.scanFont({ scanLineOf(o, line) }, context);
}

QList<ScanFont::Fit> EditOpener::scanFitsIn(EditOpen &o, const QList<TextLayout::OriginalLine> &texts, const QString &family)
{
    QList<ScanFont::Line> all;
    for (const TextLayout::OriginalLine &line : texts) all.append(scanLineOf(o, line));
    const ScanFont::Match m = o.scanFont(all, {}, family);
    return m.lines.size() == texts.size() ? m.lines : QList<ScanFont::Fit>(texts.size());
}

bool EditOpener::narrowScanLines(EditOpen &o, QList<TextLayout::OriginalLine> &texts, ScanFont::Match &face, int wide)
{
    // A scanned paragraph holds only the lines set like the one clicked: in
    // the face found, at its size and weight, and covered about as well.
    bool taken = false;
    int at = 0;
    for (int i = 1; i < texts.size(); ++i)
        if (distanceTo(texts.at(i).rect, o.clickPt) < distanceTo(texts.at(at).rect, o.clickPt)) at = i;
    QList<ScanFont::Fit> in = scanFitsIn(o, texts, face.family);
    if (in.at(at).score < kMisread && rereadScanLine(o, texts[at])) {
        taken = true;
        in = scanFitsIn(o, texts, face.family);
    }
    if (at != wide && in.at(at).score < kKnownFace) {
        // The clicked line is set in another face than the widest one when
        // the face found on it covers it better, and well: a line OCR
        // misread fits no face.
        const ScanFont::Match own = scanFaceOn(o, texts.at(at));
        if (own.isValid() && own.family != face.family) {
            const QList<ScanFont::Fit> mine = scanFitsIn(o, texts, own.family);
            if (mine.at(at).sizePt > 0.0 && mine.at(at).score >= kMisread
                    && (in.at(at).sizePt <= 0.0 || mine.at(at).score > in.at(at).score)) {
                face = own;
                in = mine;
            }
        }
    }
    QList<double> strokes;
    for (const TextLayout::OriginalLine &line : std::as_const(texts)) strokes.append(o.strokeWidth(line.rect));
    QList<QRectF> rects;
    for (const TextLayout::OriginalLine &line : std::as_const(texts)) rects.append(line.rect);
    QList<bool> ruled;
    for (int i = 0; i + 1 < texts.size(); ++i) ruled.append(o.ruledBetween(texts.at(i).rect, texts.at(i + 1).rect));
    const auto [first, last] = paragraphAround(in, strokes, rects, ruled, at);
    if (first > 0 || last + 1 < texts.size()) {
        texts = texts.mid(first, last - first + 1);
        in = in.mid(first, last - first + 1);
        taken = true;
    }
    face.lines = in;
    // A line left alone is matched as one, with the lines nearby.
    if (texts.size() == 1)
        if (const ScanFont::Match alone = scanFaceOn(o, texts.first()); alone.isValid() && alone.lines.value(0).sizePt > 0.0) face = alone;
    return taken;
}

bool EditOpener::partScanFigures(EditOpen &o, QList<TextLayout::OriginalLine> &texts, ScanFont::Match &face)
{
    // A number and a word read as one are parted where the scan parts them, and the lines are measured again.
    const double slope = scanSlope(face.lines);
    QFont font(face.family);
    font.setPixelSize(1000);
    font.setBold(face.bold);
    font.setItalic(face.italic);
    const double space = QFontMetricsF(font).horizontalAdvance(u' ') * face.sizePt / 1000.0;
    // The white along a line's slope, from above its capitals to below its descenders.
    const auto gapsOf = [&](int i) {
        const QRectF &rect = texts.at(i).rect;
        const ScanFont::Fit &fit = face.lines.at(i);
        const double stand = fit.sizePt > 0.0 ? scanStand(fit, slope) : texts.at(i).baseline - slope * rect.left();
        const double left = stand + slope * rect.left(), right = stand + slope * rect.right();
        return o.gaps(QRectF(QPointF(rect.left(), qMin(left, right) - face.sizePt * 0.78),
                             QPointF(rect.right(), qMax(left, right) + face.sizePt * 0.22)), slope);
    };
    if (!ScanCleanup::partFigures(texts, space, gapsOf)) return false;
    const QList<ScanFont::Fit> again = scanFitsIn(o, texts, face.family);
    if (again.size() == face.lines.size()) face.lines = again;
    return true;
}

void EditOpener::matchScanFace(EditOpen &o)
{
    bool taken = false;
    QList<TextLayout::OriginalLine> texts = scanParagraph(o, taken);
    if (o.framedLines.isEmpty()) {
        // Only the line the click fell on or nearest to is looked at where the click was.
        qsizetype clicked = 0;
        for (qsizetype i = 1; i < texts.size(); ++i)
            if (distanceTo(texts.at(i).rect, o.clickPt) < distanceTo(texts.at(clicked).rect, o.clickPt)) clicked = i;
        QList<bool> read;
        for (qsizetype i = 0; i < texts.size(); ++i) read.append(rereadSpaced(o, texts[i], i == clicked));
        if (texts.size() == 1 && keepFigures(o, texts.first())) read[0] = true;
        // Letters each apart at a like step are a row whatever reading them set close gave: closed up they
        // keep their step where something is typed into or behind them.
        for (qsizetype i = 0; i < texts.size(); ++i)
            if (!read.at(i) && closeApart(o, texts[i])) read[i] = true;
        taken = taken || read.contains(true);
        // What was read here is a form's rule, and no letters stand at the click: there is no text to open.
        if (texts.size() == 1 && !read.first() && o.ruleAlone(texts.first().rect)) {
            o.ruleOnly = true;
            return;
        }
        // A row read whole takes in what OCR had as pieces of it.
        const auto within = [&](qsizetype i, qsizetype j) {
            const double wider = texts.at(j).rect.width() - texts.at(i).rect.width();
            return j != i && read.at(j) && texts.at(j).rect.adjusted(-1.0, -1.0, 1.0, 1.0).contains(texts.at(i).rect.center())
                && (wider > 0.5 || (wider > -0.5 && j < i));
        };
        for (qsizetype i = texts.size() - 1; i >= 0; --i)
            for (qsizetype j = 0; j < texts.size(); ++j)
                if (within(i, j)) {
                    texts.removeAt(i);
                    read.removeAt(i);
                    break;
                }
    }
    // The edit holds the lines taken.
    const auto takeTexts = [&]() {
        if (!taken) return;
        const auto [text, bounds] = joinedLines(texts);
        o.displayText = o.originalText = o.pdfText = o.block.text = text;
        o.block.pdfBounds = o.bounds = o.originalBounds = o.eraseBounds = bounds;
        o.paragraphBounds = texts.size() > 1;
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
    // The face is found on the widest line.
    int wide = 0;
    for (int i = 1; i < texts.size(); ++i)
        if (texts.at(i).rect.width() > texts.at(wide).rect.width()) wide = i;
    ScanFont::Match face = scanFaceOn(o, texts.at(wide));
    // A line no face covers was misread, also by the OCR that set it in the scan's face.
    if (texts.size() == 1 && o.framedLines.isEmpty() && face.isValid() && face.lines.value(0).score < kMisread
            && rereadScanLine(o, texts.first())) {
        taken = true;
        face = scanFaceOn(o, texts.at(wide));
    }
    if (!face.isValid()) {
        takeTexts();
        return;
    }
    const QString widest = texts.at(wide).text;
    const double covered = face.lines.value(texts.size() > 1 ? 0 : wide).score;
    if (texts.size() > 1 && o.framedLines.isEmpty()) taken = narrowScanLines(o, texts, face, wide) || taken;
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
    // Letters printed apart keep their spacing, where most lines have it: a
    // line OCR misread can measure apart alone.
    std::sort(spacings.begin(), spacings.end());
    if (const double apart = spacings.at((spacings.size() - 1) / 2); apart > 0.2) o.box.characterSpacingPt = apart;
    // A face found on a line long enough to tell, and covering it well, is tried first on the document's other lines.
    constexpr double kSure = 0.7;
    constexpr qsizetype kLetters = 20;
    if (covered >= kSure && widest.size() >= kLetters) {
        m_faces.families.removeAll(face.family);
        m_faces.families.prepend(face.family);
        if (m_faces.families.size() > 3) m_faces.families.removeLast();
    }
    if (o.framedLines.isEmpty() && face.lines.size() == texts.size() && partScanFigures(o, texts, face)) taken = true;
    o.scanFace = face;
    o.fontFamily = face.family;
    o.bold = face.bold;
    o.italic = face.italic;
    o.fontSizePt = face.sizePt;
    if (o.framedLines.isEmpty() && face.lines.size() == texts.size() && placeScanLines(o, texts, face)) {
        taken = true;
        takeTexts();
    }
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
        // A reading is taken only where its letters cover the scan fairly well:
        // where none does, the fit failed, not the reading.
        constexpr double kBetter = 0.02, kFits = 0.4;
        if (s > best + kBetter && s >= kFits) { best = s; taken = again; }
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
    // In a table of figures the nearest are figures too, which tell little of a face: some lettered ones stay among them.
    QList<ScanFont::Line> out;
    int figures = 0;
    for (const auto &[rect, text] : std::as_const(near)) {
        bool own = false;
        for (const TextLayout::OriginalLine &t : taken) own |= t.rect.intersects(rect);
        if (own || text.trimmed().isEmpty()) continue;
        const bool lettered = std::count_if(text.cbegin(), text.cend(), [](QChar c) { return c.isLetter(); }) >= 4;
        if (!lettered && figures >= 5) continue;
        figures += !lettered;
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
        if ((!backend->hasSelectableText(near) || backend->scanBelowText(near)) && !backend->hasScanText(near))
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

#endif
