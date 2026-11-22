#include "engine/edit/ScanFont.hpp"

#include "engine/edit/ScanFit.hpp"
#include "engine/edit/ScanLine.hpp"

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QStringList>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

// One family of each metric group that is installed: the faces scans are printed in.
const QStringList &candidates()
{
    static const QStringList families = [] {
        const QList<QStringList> groups = {
            { "Arial", "Liberation Sans", "Arimo", "Nimbus Sans", "Helvetica" },
            { "Times New Roman", "Liberation Serif", "Tinos", "Nimbus Roman", "Times" },
            // Liberation Mono keeps Courier's widths but not its thin slab-serif strokes.
            { "Courier New", "Nimbus Mono PS", "Courier" },
            { "Liberation Mono", "Cousine" },
            { "Calibri", "Carlito" },
            { "Cambria", "Caladea" },
            { "Verdana", "DejaVu Sans" },
            { "DejaVu Serif" },
            { "Georgia", "Gelasio" },
            { "Tahoma" },
            { "Segoe UI" },
            { "Trebuchet MS" },
            { "Garamond", "EB Garamond" },
            { "Noto Sans" },
            { "Noto Serif" },
            { "Open Sans" },
        };
        // hasFamily() misses a family installed by two foundries, as "Nimbus Mono PS [UKWN]".
        const auto installed = [](const QString &family) {
            const QString found = QFontInfo(QFont(family)).family();
            return found.section(QStringLiteral(" ["), 0, 0).compare(family, Qt::CaseInsensitive) == 0;
        };
        QStringList out;
        for (const QStringList &group : groups)
            for (const QString &family : group)
                if (QFontDatabase::hasFamily(family) || installed(family)) {
                    out.append(family);
                    break;
                }
        return out;
    }();
    return families;
}

// Faces seldom printed in office documents win only where they fit clearly better.
double rarity(const QString &family)
{
    static const QStringList rare { "Noto Sans", "Noto Serif", "Open Sans" };
    return rare.contains(family) ? 0.015 : 0.0;
}

using ScanFit::fitLine;
using ScanLine::Scan;

// A face is tried regular, bold and italic: a paragraph printed italic
// matches no upright cut well, and another face's upright cut can win.
QFont cutOf(const QString &family, int cut, bool kerned)
{
    QFont font = ScanFit::faceOf(family, cut == 1, kerned);
    font.setItalic(cut == 2);
    return font;
}

// The lines a face is matched on, its own first and then lines nearby, as measured on the scan.
struct Measured {
    const QImage &img;
    qreal scale;
    qsizetype own;
    QList<ScanFont::Line> all;
    QList<Scan> scans;
    QList<QStringList> words;
    int black { -1 };

    void take(const ScanFont::Line &line)
    {
        all.append(line);
        words.append(line.text.split(u' ', Qt::SkipEmptyParts));
        const QRect px = QRectF(line.inkPt.topLeft() * scale, line.inkPt.size() * scale)
                             .toAlignedRect().adjusted(-2, -2, 2, 2);
        const QRectF text = line.textPt.isEmpty() ? line.inkPt : line.textPt;
        const int top = qFloor(text.top() * scale) - px.top(), bottom = qCeil(text.bottom() * scale) - px.top();
        QList<double> starts;
        for (const double x : line.wordStartsPt) starts.append(x * scale);
        const double base = line.basePt >= 0.0 ? line.basePt * scale - px.top() : -1.0;
        scans.append(words.last().isEmpty() ? Scan{} : ScanLine::scanOf(img, px, words.last(), top, bottom, starts, base, line.tilt));
        // What another edit left on the line as pictures of its own is not the scan's print: by columns of the sample.
        const int left = px.intersected(img.rect()).left();
        for (const QRectF &r : line.foreignPt)
            scans.last().foreign.append({ qFloor(r.left() * scale) - left, qCeil(r.right() * scale) - left });
        all.last().text = words.last().join(u' ');
    }

    void dropLast()
    {
        all.removeLast();
        words.removeLast();
        scans.removeLast();
    }

    double weightOf(int i, double nearby) const
    {
        return (scans.at(i).right - scans.at(i).left) * (i < own ? 1.0 : nearby);
    }

    QList<int> widest(int from, int to, int count) const
    {
        QList<int> out;
        for (int i = from; i < to; ++i)
            if (scans.at(i).isValid()) out.append(i);
        std::sort(out.begin(), out.end(), [&](int a, int b) {
            return scans.at(a).right - scans.at(a).left > scans.at(b).right - scans.at(b).left;
        });
        return out.mid(0, count);
    }

    double grayOf(const QString &name, bool kerned, const QList<int> &on, double nearby) const
    {
        // How well a face covers some lines, compared in gray and a little
        // blurred, so a pixel of offset does not decide.
        double best = -1.0;
        for (const int cut : { 0, 1, 2 }) {
            const QFont font = cutOf(name, cut, kerned);
            double score = 0.0, weight = 0.0;
            for (const int i : on) {
                double gray = -1.0;
                for (const double sigma : { 0.82, 1.41 })
                    gray = qMax(gray, fitLine(scans.at(i), words.at(i), all.at(i).text, font, scale, false, 1.0, sigma).score);
                if (gray <= -1.0) continue;
                score += gray * weightOf(i, nearby);
                weight += weightOf(i, nearby);
            }
            if (weight > 0.0) best = qMax(best, score / weight);
        }
        return best;
    }

    QList<std::pair<double, QString>> rank(const QList<int> &on, double nearby, bool &kerned, bool findKerning,
                                           bool every = false) const
    {
        // The cheap cover sorts the faces first; the gray compares the best few.
        // Documents are set with kerning or without, as their program does; it
        // is found once, on the face the cheap cover puts first.
        QList<std::pair<double, QString>> rough;
        for (const QString &name : candidates()) {
            double score = -1.0;
            for (const int cut : { 0, 1, 2 }) {
                const QFont font = cutOf(name, cut, false);
                double sum = 0.0, weight = 0.0;
                for (const int i : on) {
                    const ScanFont::Fit fit = fitLine(scans.at(i), words.at(i), all.at(i).text, font, scale);
                    if (fit.sizePt <= 0.0) continue;
                    sum += fit.score * weightOf(i, nearby);
                    weight += weightOf(i, nearby);
                }
                if (weight > 0.0) score = qMax(score, sum / weight);
            }
            if (score > -1.0) rough.append({ score, name });
        }
        std::sort(rough.begin(), rough.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        if (findKerning && !rough.isEmpty())
            kerned = grayOf(rough.first().second, true, on, 1.0) > grayOf(rough.first().second, false, on, 1.0);
        // The best four, and up to three more the cheap cover leaves as close: thin strokes cover a coarse
        // scan badly. A few figures alone are compared in every face.
        constexpr double kAsClose = 0.05;
        QList<std::pair<double, QString>> ranked;
        for (int k = 0; k < (every ? int(rough.size()) : qMin(7, int(rough.size()))); ++k) {
            if (!every && k >= 4 && rough.at(0).first - rough.at(k).first > kAsClose) break;
            const double score = grayOf(rough.at(k).second, kerned, on, nearby);
            if (score > -1.0) ranked.append({ score - rarity(rough.at(k).second), rough.at(k).second });
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        return ranked;
    }
};

}

namespace ScanFont {

// How much ink a line's scan holds against a face drawn where the line stands.
static double inkShare(const Measured &m, int i, const QFont &face, double blur = 0.82)
{
    double inked = 0.0, scanned = 0.0;
    fitLine(m.scans.at(i), m.words.at(i), m.all.at(i).text, face, m.scale, false, 1.0, -1.0, nullptr, blur, false,
            nullptr, &inked);
    // Within the line's own ink alone: a field's line reaches far past its entry, and all that paper's grain would count.
    const Scan &s = m.scans.at(i);
    for (int y = s.top; y <= s.bottom; ++y)
        for (int x = s.left; x <= s.right; ++x) scanned += s.dark[size_t(y) * size_t(s.ink.w) + size_t(x)];
    return inked > 0.0 ? scanned / inked : 0.0;
}

// How far the ink a face draws is from the ink the lines hold, in the weight and under the blur that come closest.
static double inkOff(const Measured &m, const QString &family, bool kerned, const QList<int> &own)
{
    double least = 1e9;
    for (const bool bold : { false, true })
        for (const double blur : { 0.4, 0.82, 1.41, 2.16 }) {
            // Print and scan only add to a stroke: a scan heavier than the face tells nothing against it. And of
            // several lines most tell, as one of them may be misread or no text.
            QList<double> off;
            for (const int i : own)
                if (const double share = inkShare(m, i, ScanFit::faceOf(family, bold, kerned), blur); share > 0.0)
                    off.append(qMax(0.0, -std::log(share)));
            std::sort(off.begin(), off.end());
            if (!off.isEmpty()) least = qMin(least, off.at((off.size() - 1) / 2));
        }
    return least;
}

// Figures look alike in most faces, how thick they are drawn does not: each face counts for less the
// farther its strokes are from holding the scan's ink.
static QString byInk(const Measured &m, const QList<std::pair<double, QString>> &ranked, bool kerned, const QList<int> &own)
{
    // A scan set too light, or letters drawn fitted to its pixels, hold a fifth less ink than their face: only
    // a face heavier than the scan by more than that tells against itself.
    constexpr double kInkCounts = 0.4, kInkNear = 0.25;
    QString family = ranked.first().second;
    double top = -1e9;
    for (const auto &[gray, name] : ranked)
        if (const double score = gray - kInkCounts * qMax(0.0, inkOff(m, name, kerned, own) - kInkNear); score > top) {
            top = score;
            family = name;
        }
    return family;
}

// The ink the lines nearby hold against the regular face, the usual one of them.
static double usualShare(const Measured &m, const QFont &regular)
{
    QList<double> nearby;
    for (int i = 1; i < m.all.size(); ++i)
        if (m.scans.at(i).isValid())
            if (const double held = inkShare(m, i, regular); held > 0.0) nearby.append(held);
    std::sort(nearby.begin(), nearby.end());
    return nearby.size() >= 2 ? nearby.at(nearby.size() / 2) : 0.0;
}

// A heading of a word or two printed bold is as wide as a larger regular
// one. It is bold where it holds more ink against the regular face than
// the lines nearby hold against theirs, and the bold face comes closer to
// what a bold line would hold.
static bool boldHeading(const Measured &m, const QFont &regular, const QFont &bold)
{
    const double usual = usualShare(m, regular), own = inkShare(m, 0, regular), heavy = inkShare(m, 0, bold);
    // Strokes thickened by the scan add less to a bold face's ink than to a regular one's.
    const double usualBold = 1.0 + (usual - 1.0) * 0.6;
    return usual > 0.0 && heavy > 0.0 && own > usual * 1.08
        && std::abs(std::log(heavy / usualBold)) < std::abs(std::log(own / usual));
}

// A few figures found bold on a coarse scan are regular where they hold no more ink against the regular
// face than the lines nearby hold against theirs: the scan thickened them all alike.
static bool thickenedFigures(const Measured &m, const QFont &regular)
{
    const double usual = usualShare(m, regular);
    return usual > 0.0 && inkShare(m, 0, regular) <= usual * 1.08;
}

// The lines nearby whose ink is about as tall as the line's own, four at most, tried on the faces the line itself tells.
static void takeNearby(Measured &m, const QList<Line> &context, int height, double taller, bool figures,
                       const QList<std::pair<double, QString>> &ranked, bool kerned)
{
    // Rows of figures in the line's face are covered well by it: one covered worse is another's or misread.
    const double kMisread = figures ? 0.75 : 0.6;
    const qsizetype own = m.all.size();
    for (int i = 0; i < context.size() && m.all.size() < own + 4; ++i) {
        // Figures look alike in most faces: a line nearby has its say only with letters, but for
        // a row of figures, which other rows of them tell more about than it does alone.
        const QString &text = context.at(i).text;
        if (std::count_if(text.cbegin(), text.cend(), [figures](QChar c) { return figures ? c.isLetterOrNumber() : c.isLetter(); }) < 4) continue;
        m.take(context.at(i));
        const Scan &s = m.scans.last();
        // Figures stand as high as capitals: a lettered line has ascenders and descenders on top of that.
        const int more = s.isValid() ? s.bottom - s.top + 1 - height : 0;
        // A smudge among a row's figures stands higher than they do: rows a good deal lower still count for it.
        bool fits = s.isValid() && (figures ? more >= -height * 0.35 && more <= height * taller : std::abs(more) <= height * taller);
        // A line none of the line's best faces covers was misread, as a form's rule taken for words. A row
        // of figures is asked itself instead: a few figures tell too little of which faces are theirs.
        if (fits && figures) {
            bool as = kerned;
            const QList<std::pair<double, QString>> own1 = m.rank({ int(m.all.size()) - 1 }, 1.0, as, false, true);
            fits = !own1.isEmpty() && own1.first().first >= kMisread;
        } else if (fits && !ranked.isEmpty()) {
            fits = false;
            for (int k = 0; k < qMin(2, int(ranked.size())) && !fits; ++k)
                fits = m.grayOf(ranked.at(k).second, kerned, { int(m.all.size()) - 1 }, 1.0) >= kMisread;
        }
        if (!fits) m.dropLast();
    }
}

// The weight of a line alone where its own fit tells it badly: a short heading by the ink of the lines
// nearby, a few figures by theirs too or by the ink the two weights would hold, and misread ones as
// the rows nearby are set, in weight and size.
static void settleWeight(const Measured &m, Match &best, bool figures, const QFont &regular, const QFont &bold,
                         double sigma, double nearSize, int asNearby)
{
    Fit &line = best.lines.first();
    if (line.sizePt <= 0.0) return;
    const auto fitted = [&](int weight) {
        return ScanFit::fitStyled(m.scans.first(), m.words.first(), m.all.first().text, regular, bold, m.scale, sigma, nearSize, weight);
    };
    if (asNearby != 0) {
        if (line.bold != (asNearby > 0)) line = fitted(asNearby);
        if (nearSize > 0.0 && line.sizePt > 0.0 && std::abs(std::log(line.sizePt / nearSize)) > 0.08) line.sizePt = nearSize;
    } else if (!figures && m.all.size() >= 3 && m.words.first().size() <= 2 && !line.bold && boldHeading(m, regular, bold)) {
        line = fitted(1);
    } else if (figures && line.bold) {
        // By the lines nearby, or alone by the ink the two weights would hold, each under the blur that suits it best.
        const auto off = [&](const QFont &face) {
            double least = 1e9;
            for (const double blur : { 0.82, 1.41, 2.16 })
                if (const double share = inkShare(m, 0, face, blur); share > 0.0) least = qMin(least, std::abs(std::log(share)));
            return least;
        };
        if (m.all.size() >= 3 ? thickenedFigures(m, regular) : off(regular) <= off(bold)) line = fitted(-1);
    }
}

Match match(const QImage &img, qreal scale, const QList<Line> &lines, const QList<Line> &context,
            const QString &family, int blackTone)
{
    constexpr qsizetype kFew = 16;
    if (img.isNull() || scale <= 0.0 || lines.isEmpty()) return {};
    Measured m { img, scale, lines.size(), {}, {}, {}, blackTone };
    int height = 0;
    qsizetype letters = 0;
    for (const Line &line : lines) {
        m.take(line);
        letters += m.all.last().text.size();
        if (m.scans.last().isValid()) height = qMax(height, m.scans.last().bottom - m.scans.last().top + 1);
    }
    // The face is chosen on the widest of its own lines.
    const QList<int> own = m.widest(0, int(lines.size()), 3);
    bool kerned = false;
    if (!family.isEmpty()) kerned = m.grayOf(family, true, own, 1.0) > m.grayOf(family, false, own, 1.0);
    // A few figures: none of it letters, or a letter or two among three figures and more, as OCR reads
    // from a speck or a smudge between them.
    qsizetype digits = 0, alphas = 0;
    for (const Line &line : lines)
        for (const QChar c : line.text) { digits += c.isDigit(); alphas += c.isLetter(); }
    const bool figures = letters < kFew && (alphas == 0 || (digits >= 3 && alphas * 2 <= digits));
    const QList<std::pair<double, QString>> ranked = family.isEmpty() ? m.rank(own, 1.0, kerned, true, figures)
                                                                       : QList<std::pair<double, QString>>{ { 1.0, family } };
    if (ranked.isEmpty()) return {};
    Match best;
    best.family = ranked.first().second;
    // Faces the line leaves close, as when a few of its words are bold, are
    // told apart on lines nearby whose ink is about as tall. Where no face
    // covers the line well, as one OCR misread, or its letters are too small
    // or too few to tell the best faces apart, those lines choose the face,
    // also ones a descender taller or shorter; a short line takes them for its weight.
    constexpr double kClose = 0.025, kPoor = 0.7, kSmallPt = 8.5;
    const bool few = letters < kFew;
    const double lead = ranked.size() > 1 ? ranked.at(0).first - ranked.at(1).first : 1.0;
    const bool close = lead < kClose;
    const bool poor = family.isEmpty()
                   && (ranked.first().first < kPoor || height < kSmallPt * scale || (few && lead < 2.0 * kClose));
    if ((close || poor || few) && height > 0)
        takeNearby(m, context, height, figures ? 0.6 : poor || few ? 0.25 : 0.12, figures,
                   ranked.first().first >= kPoor ? ranked : QList<std::pair<double, QString>>{}, kerned);
    const QList<int> near = own + m.widest(int(lines.size()), int(m.all.size()), 4);
    if (figures && family.isEmpty()) {
        // A few figures: every face, on the row and those nearby together, the ink counting.
        const QList<std::pair<double, QString>> around = m.all.size() > lines.size() ? m.rank(near, 1.0, kerned, false, true) : ranked;
        if (!around.isEmpty()) best.family = byInk(m, around, kerned, near);
    } else if (poor && m.all.size() > lines.size()) {
        const QList<std::pair<double, QString>> around = m.rank(near, 1.0, kerned, false);
        if (!around.isEmpty()) best.family = around.first().second;
    } else if (close && m.all.size() > lines.size()) {
        double top = -1.0;
        for (int k = 0; k < qMin(3, int(ranked.size())); ++k) {
            if (ranked.at(0).first - ranked.at(k).first >= kClose) break;
            const double score = m.grayOf(ranked.at(k).second, kerned, near, 0.5) - rarity(ranked.at(k).second);
            if (score > top) { top = score; best.family = ranked.at(k).second; }
        }
    }
    if (best.family.isEmpty()) return {};
    // Each line in the weight it matches, so a bold line among regular ones shows.
    const QFont regular = ScanFit::faceOf(best.family, false, kerned), bold = ScanFit::faceOf(best.family, true, kerned);
    // The blur is the scan's, kept for all lines.
    // A few figures tell it badly: theirs is told with the rows nearby.
    const double sigma = ScanFit::blurOf(figures ? m.scans : m.scans.mid(0, lines.size()), m.words, m.all, regular, bold, scale, figures);
    // The size the lines nearby are set in, each in the weight it fits.
    QList<double> around;
    int heavy = 0;
    for (int i = int(lines.size()); i < m.all.size(); ++i) {
        if (!m.scans.at(i).isValid()) continue;
        const ScanFit::Weighed w = ScanFit::weigh(m.scans.at(i), m.words.at(i), m.all.at(i).text, regular, bold, scale, sigma);
        if (const double size = w.size[w.margin > 0.0 ? 1 : 0]; size > 0.0) around.append(size);
        heavy += w.margin > 0.0 ? 1 : -1;
    }
    std::sort(around.begin(), around.end());
    const double nearSize = around.isEmpty() ? 0.0 : around.at(around.size() / 2);
    for (int i = 0; i < lines.size(); ++i)
        best.lines.append(m.scans.at(i).isValid() ? ScanFit::fitStyled(m.scans.at(i), m.words.at(i), m.all.at(i).text,
                                                                         regular, bold, scale, sigma, nearSize)
                                                  : Fit{});
    // A few figures no face covers well, as with a smudge among them, are set as the rows nearby are.
    const bool asNearby = figures && around.size() >= 2 && ranked.first().first < kPoor;
    if (lines.size() == 1) settleWeight(m, best, figures, regular, bold, sigma, nearSize, asNearby ? (heavy > 0 ? 1 : -1) : 0);
    // A few figures tell their size to a twentieth at best: with rows nearby printed about as large they have one size.
    if (figures && lines.size() == 1 && best.lines.first().sizePt > 0.0) {
        QList<double> alike { best.lines.first().sizePt };
        for (const double size : std::as_const(around))
            if (std::abs(std::log(size / alike.first())) <= 0.1) alike.append(size);
        std::sort(alike.begin(), alike.end());
        if (alike.size() >= 3) best.lines.first().sizePt = alike.at(alike.size() / 2);
    }
    QList<double> sizes, inks;
    double weight = 0.0;
    int slanted = 0;
    for (Fit &fit : best.lines) {
        if (fit.sizePt <= 0.0) continue;
        sizes.append(fit.sizePt = ScanFit::roundedSize(fit.sizePt));
        if (fit.inkGray >= 0.0) inks.append(fit.inkGray);
        weight += fit.boldMargin;
        slanted += fit.italic ? 1 : -1;
    }
    if (sizes.isEmpty()) return {};
    std::sort(sizes.begin(), sizes.end());
    best.sizePt = sizes.at(sizes.size() / 2);
    std::sort(inks.begin(), inks.end());
    if (!inks.isEmpty()) best.inkGray = inks.at(inks.size() / 2);
    best.bold = weight > 0.0;
    best.italic = slanted > 0;
    best.kerned = kerned;
    return best;
}

}
