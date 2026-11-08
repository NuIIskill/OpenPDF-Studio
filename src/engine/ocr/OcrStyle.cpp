#include "engine/ocr/OcrStyle.hpp"

#include "engine/edit/ScanFont.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

constexpr int kRef = 256;

struct Bitmap {
    int w { 0 }, h { 0 };
    std::vector<uint8_t> on;
    bool at(int x, int y) const
    {
        return x >= 0 && y >= 0 && x < w && y < h && on[size_t(y) * size_t(w) + size_t(x)];
    }
};

struct Style {
    QString family;
    bool    bold { false };
    double  sizePx { 0.0 };
    double  baseline { 0.0 };
    double  spacingPx { 0.0 };
};

struct Rendered {
    Bitmap        ink;
    QList<QRectF> boxes;
};

// The gray halfway between an area's paper and its darkest ink.
int inkCut(const QImage &gray, const QRect &within)
{
    const QRect area = within.intersected(gray.rect());
    int hist[256] = {};
    for (int y = area.top(); y <= area.bottom(); ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = area.left(); x <= area.right(); ++x) ++hist[row[x]];
    }
    const qint64 count = qint64(area.width()) * area.height();
    int paper = 255, dark = 0, most = 0;
    qint64 seen = 0;
    for (int g = 0; g < 256; ++g)
        if (hist[g] > most) { most = hist[g]; paper = g; }
    for (int g = 0; g < 256; ++g)
        if ((seen += hist[g]) * 50 >= count) { dark = g; break; }
    return paper - dark < 50 ? -1 : (paper + dark) / 2;
}

// The ink of a scanned area between two columns, darker than the cut.
Bitmap scanInk(const QImage &gray, const QRect &area, int fromX, int toX, int cut)
{
    Bitmap b;
    b.w = area.width();
    b.h = area.height();
    b.on.assign(size_t(b.w) * size_t(b.h), 0);
    if (cut < 0) return b;
    for (int y = 0; y < b.h; ++y) {
        const uchar *row = gray.constScanLine(area.top() + y) + area.left();
        for (int x = qMax(0, fromX); x <= qMin(b.w - 1, toX); ++x)
            b.on[size_t(y) * size_t(b.w) + size_t(x)] = row[x] < cut;
    }
    return b;
}

QFont faceOf(const Style &style)
{
    QFont font(style.family);
    font.setPixelSize(kRef);
    font.setBold(style.bold);
    font.setKerning(false);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::NoFontMerging);
    const double k = style.sizePx / kRef;
    if (style.spacingPx > 0.0) font.setLetterSpacing(QFont::AbsoluteSpacing, style.spacingPx / k);
    return font;
}

// The text set in the style from the scanned ink's left edge, and each character's glyph box.
Rendered render(const Style &style, const QString &text, const QSize &size, double inkLeft)
{
    Rendered r;
    const QFont font = faceOf(style);
    const QFontMetricsF fm(font);
    const double k = style.sizePx / kRef;
    const double x0 = inkLeft - fm.tightBoundingRect(text).left() * k;
    QImage canvas(size, QImage::Format_Grayscale8);
    canvas.fill(255);
    {
        QPainter p(&canvas);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setFont(font);
        p.setPen(Qt::black);
        p.translate(x0, style.baseline);
        p.scale(k, k);
        p.drawText(QPointF(0, 0), text);
    }
    r.ink.w = size.width();
    r.ink.h = size.height();
    r.ink.on.resize(size_t(r.ink.w) * size_t(r.ink.h));
    for (int y = 0; y < r.ink.h; ++y) {
        const uchar *row = canvas.constScanLine(y);
        for (int x = 0; x < r.ink.w; ++x) r.ink.on[size_t(y) * size_t(r.ink.w) + size_t(x)] = row[x] < 128;
    }
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isSpace() || c.isLowSurrogate()) {
            r.boxes.append(QRectF());
            continue;
        }
        const QString glyph = c.isHighSurrogate() ? text.mid(i, 2) : QString(c);
        const QRectF tight = fm.tightBoundingRect(glyph);
        const double x = fm.horizontalAdvance(text.left(i));
        r.boxes.append(QRectF(x0 + (x + tight.left()) * k, style.baseline + tight.top() * k,
                              tight.width() * k, tight.height() * k));
    }
    return r;
}

struct Placed { qint64 wrong { -1 }; int dx { 0 }; int dy { 0 }; };

// The pixels where scan and rendering disagree, at the best of the given shifts.
Placed placeOn(const Bitmap &scan, const Bitmap &drawn, int dxFrom, int dxTo, int dyFrom, int dyTo)
{
    Placed best;
    for (int dy = dyFrom; dy <= dyTo; ++dy)
        for (int dx = dxFrom; dx <= dxTo; ++dx) {
            qint64 wrong = 0;
            for (int y = 0; y < scan.h; ++y)
                for (int x = 0; x < scan.w; ++x)
                    wrong += scan.on[size_t(y) * size_t(scan.w) + size_t(x)] != drawn.at(x - dx, y - dy);
            if (best.wrong < 0 || wrong < best.wrong) best = { wrong, dx, dy };
        }
    return best;
}

// The pixels where two renderings, each at its shift, differ.
qint64 apart(const Bitmap &a, const Placed &pa, const Bitmap &b, const Placed &pb)
{
    qint64 n = 0;
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < a.w; ++x)
            n += a.at(x - pa.dx, y - pa.dy) != b.at(x - pb.dx, y - pb.dy);
    return n;
}

// What OCR reads in place of each character, besides the character itself.
QString confusedWith(QChar c)
{
    static const QHash<QChar, QString> table = [] {
        QHash<QChar, QString> t;
        const auto group = [&t](const QString &members) {
            for (const QChar m : members)
                for (const QChar o : members)
                    if (o != m && !t[m].contains(o)) t[m] += o;
        };
        for (const QString &g : { QStringLiteral("-–—"), QStringLiteral("0oO©"), QStringLiteral("8§$"),
                                  QStringLiteral("S5"), QStringLiteral("B8"), QStringLiteral("Z2"),
                                  QStringLiteral("G6"), QStringLiteral("g9"),
                                  QStringLiteral("Il1|"), QStringLiteral("cC"), QStringLiteral("sS"),
                                  QStringLiteral("vV"), QStringLiteral("wW"), QStringLiteral("xX"),
                                  QStringLiteral("zZ"), QStringLiteral("eéèêë"), QStringLiteral("aàâäá"),
                                  QStringLiteral("oöôó"), QStringLiteral("uüùûú"), QStringLiteral("cç"),
                                  QStringLiteral("nñ"), QStringLiteral("EÉÈÊ"), QStringLiteral("AÀÂÄ"),
                                  QStringLiteral("OÖÔ"), QStringLiteral("UÜÙÛ"), QStringLiteral("CÇ") })
            group(g);
        return t;
    }();
    return table.value(c);
}

// The share of the pixels two readings differ in that the scan must side
// with before another reading replaces OCR's, in tenths. OCR is right far
// more often than not; it is overruled readily only where it errs by habit:
// a dash read as a hyphen, a section sign as 8 or $, a capital O as the
// copyright sign, a letter o as a zero and an accent left off an e.
int strictness(QChar from, QChar to, QChar before, QChar after, bool first)
{
    const auto in = [](QChar c, const char16_t *set) { return std::u16string_view(set).find(c.unicode()) != std::u16string_view::npos; };
    // A digit among letters or a letter among digits is no reading; a word's
    // first letter may be a capital before lowercase ones.
    const bool letters = (before.isLetter() || after.isLetter()) && !before.isDigit() && !after.isDigit();
    const bool digits = (before.isDigit() || after.isDigit()) && !before.isLetter() && !after.isLetter();
    if ((letters && to.isDigit()) || (digits && to.isLetter() && to != u'§')) return 9;
    if ((letters && from.isDigit()) || (digits && from.isLetter())) return 3;
    const bool lowerAround = (before.isLower() && !in(before, u"Il1|")) || (after.isLower() && !in(after, u"Il1|") && !first);
    if (in(from, u"Il1|") && in(to, u"Il1|") && lowerAround) return to == u'l' ? 3 : 9;
    if (in(from, u"-") && in(to, u"–—")) return 5;
    if (in(from, u"8$") && to == u'§') return 5;
    if (from == u'©' && in(to, u"Oo")) return 5;
    if (from == u'0' && in(to, u"oO")) return 5;
    if (from == u'e' && in(to, u"éè")) return 7;
    // A mark OCR read is changed or taken away only when the scan has none of it.
    if (from.decompositionTag() == QChar::Canonical) return 9;
    return 8;
}

// The word's reading among those OCR confuses that, set in the line's face,
// disagrees least with the scan; the letter boxes come from that setting.
// Returns how much of the scan the reading explains, or -1 when it was not tried.
double settleWord(const QImage &gray, const Style &style, OcrEngine::Word &word, double rightLimit,
                  bool pointFollows)
{
    const QRectF box = word.pdfBounds;
    const double pad = style.sizePx * 0.4;
    const QRect area = QRectF(box.left() - pad, word.baseline - style.sizePx * 1.15,
                              box.width() + 2.0 * pad, style.sizePx * 1.55)
                           .toAlignedRect().intersected(gray.rect());
    if (area.width() < 4 || area.height() < 4) return -1.0;
    // Only this word's ink counts, with a point OCR may have left out after
    // it; a wider reading pays for the paper it covers.
    const double right = qMin(rightLimit - style.sizePx * 0.15, box.right() + style.sizePx * 0.35);
    const Bitmap scan = scanInk(gray, area, qFloor(box.left()) - area.left() - 1,
                                qCeil(right) - area.left(), inkCut(gray, area));
    int inkLeft = scan.w;
    qint64 scanInkCount = 0;
    for (int y = 0; y < scan.h; ++y)
        for (int x = 0; x < scan.w; ++x)
            if (scan.on[size_t(y) * size_t(scan.w) + size_t(x)]) {
                inkLeft = qMin(inkLeft, x);
                ++scanInkCount;
            }
    if (inkLeft >= scan.w) return -1.0;

    Style local = style;
    local.baseline = word.baseline - area.top();
    QString text = word.text;
    Rendered drawn = render(local, text, area.size(), inkLeft);
    Placed at = placeOn(scan, drawn.ink, -3, 3, -2, 2);
    qint64 drawnInk = 0;
    for (const uint8_t v : drawn.ink.on) drawnInk += v;
    const double explained = 1.0 - double(at.wrong) / double(qMax<qint64>(1, scanInkCount + drawnInk));
    // A word the face does not explain is left as OCR read it.
    constexpr double kExplained = 0.35;
    if (explained < kExplained) return explained;

    // Each round takes the reading the scan sides with most clearly and
    // weighs the others again against it, so a letter that shifts all after
    // it, as a dash read as a hyphen does, is settled before those.
    for (;;) {
        QString taken;
        Rendered takenDrawn;
        Placed takenAt;
        qint64 margin = 0;
        for (qsizetype i = 0; i < text.size(); ++i) {
            const QChar c = text.at(i);
            QStringList candidates;
            for (const QChar t : confusedWith(c)) candidates.append(QString(text).replace(i, 1, t));
            // A space OCR left out after an abbreviation's point.
            if ((c == u'.' || c == u',' || c == u':' || c == u';') && i + 1 < text.size() && !text.at(i + 1).isSpace())
                candidates.append(QString(text).insert(i + 1, u' '));
            // The point after an abbreviation OCR left out.
            if (c.isLetter() && ((i == text.size() - 1 && !pointFollows)
                                 || (i + 1 < text.size() && (text.at(i + 1) == u',' || text.at(i + 1) == u';'))))
                candidates.append(QString(text).insert(i + 1, u'.'));
            for (const QString &candidate : std::as_const(candidates)) {
                Rendered r = render(local, candidate, area.size(), inkLeft);
                const Placed p = placeOn(scan, r.ink, at.dx - 2, at.dx + 2, at.dy - 2, at.dy + 2);
                // Another reading wins where the scan sides with it on most of
                // the pixels the two readings differ in; readings that look
                // alike in this face are never decided by noise.
                const qint64 differ = apart(drawn.ink, at, r.ink, p);
                // A character OCR left out must be there on nearly all of its pixels;
                // a space after an abbreviation's point on most.
                int tenths = candidate.size() > text.size() ? (candidate.at(i + 1).isSpace() ? 5 : 8)
                    : strictness(c, candidate.at(i), i > 0 ? text.at(i - 1) : QChar(),
                                 i + 1 < text.size() ? text.at(i + 1) : QChar(), i == 0);
                const qint64 needed = qMax<qint64>(8, differ * tenths / 10);
                const qint64 gain = at.wrong - p.wrong;
                if (gain >= needed && gain - needed >= margin) {
                    margin = gain - needed;
                    taken = candidate;
                    takenDrawn = std::move(r);
                    takenAt = p;
                }
            }
        }
        if (taken.isEmpty()) break;
        text = taken;
        drawn = std::move(takenDrawn);
        at = takenAt;
    }

    word.text = text;
    word.chars.clear();
    QRectF bounds;
    for (const QRectF &b : std::as_const(drawn.boxes)) {
        const QRectF placed = b.isEmpty() ? QRectF() : b.translated(area.left() + at.dx, area.top() + at.dy);
        word.chars.append(placed);
        if (!placed.isEmpty()) bounds = bounds.isNull() ? placed : bounds.united(placed);
    }
    if (!bounds.isEmpty()) word.pdfBounds = bounds;
    return explained;
}

Style styleOf(const ScanFont::Match &m, qreal scale)
{
    Style style;
    const ScanFont::Fit &fit = m.lines.first();
    style.family = m.family;
    style.bold = fit.bold;
    style.sizePx = fit.sizePt * scale;
    style.spacingPx = fit.spacingPt * scale;
    return style;
}

// The face of a few words of a line, found among the installed faces.
ScanFont::Match lineFace(const QImage &gray, qreal scale, const OcrEngine::Line &line)
{
    constexpr int kEnough = 28;
    QRectF box;
    QStringList words;
    int chars = 0;
    for (const OcrEngine::Word &w : line) {
        if (chars >= kEnough) break;
        box = box.isNull() ? w.pdfBounds : box.united(w.pdfBounds);
        words.append(w.text);
        chars += int(w.text.size()) + 1;
    }
    if (box.isEmpty()) return {};
    const QRectF pt(box.topLeft() / scale, box.size() / scale);
    const double h = pt.height();
    const ScanFont::Match m = ScanFont::match(gray, scale, { { pt.adjusted(0.0, -h * 0.5, 0.0, h * 0.6),
                                                               words.join(u' '), pt } });
    return m.isValid() && !m.lines.isEmpty() && m.lines.first().sizePt > 0.0 ? m : ScanFont::Match{};
}

}

namespace OcrStyle {

void settle(const QImage &gray, qreal scale, QList<OcrEngine::Line> &lines,
            const std::function<bool()> &cancelled, const std::function<void(qreal done)> &progress)
{
    if (gray.isNull() || scale <= 0.0) return;
    // A paragraph is set in one face, found on its widest line; a line it
    // explains badly gets a face of its own.
    QHash<int, QList<int>> paragraphs;
    for (int l = 0; l < lines.size(); ++l)
        if (!lines.at(l).isEmpty()) paragraphs[lines.at(l).first().paragraph].append(l);
    constexpr double kFits = 0.6;
    int settled = 0;
    for (const QList<int> &members : std::as_const(paragraphs)) {
        if (cancelled && cancelled()) return;
        const auto width = [&](int l) {
            return lines.at(l).last().pdfBounds.right() - lines.at(l).first().pdfBounds.left();
        };
        const int widest = *std::max_element(members.cbegin(), members.cend(),
                                             [&](int a, int b) { return width(a) < width(b); });
        const ScanFont::Match face = lineFace(gray, scale, lines.at(widest));
        for (const int l : members) {
            if (progress) progress(qreal(settled++) / lines.size());
            OcrEngine::Line kept = lines.at(l);
            double sum = 0.0;
            int tried = 0;
            const auto settleLine = [&](const ScanFont::Match &m) {
                OcrEngine::Line &line = lines[l];
                for (int k = 0; k < line.size(); ++k) {
                    const bool next = k + 1 < line.size();
                    const double limit = next ? line.at(k + 1).pdfBounds.left() - 1.0 : gray.width();
                    const bool point = next && QStringLiteral(".,;:").contains(line.at(k + 1).text.front());
                    const double e = settleWord(gray, styleOf(m, scale), line[k], limit, point);
                    if (e >= 0.0) { sum += e; ++tried; }
                }
            };
            if (face.isValid()) settleLine(face);
            if (tried && sum / tried >= kFits) continue;
            const ScanFont::Match own = lineFace(gray, scale, kept);
            lines[l] = kept;
            if (own.isValid()) settleLine(own);
        }
    }
}

}
