#pragma once

#ifdef HAVE_PDF_RENDERING

#include "engine/document/PdfBackend.hpp"
#include "engine/edit/ContentModel.hpp"
#include "engine/edit/EditSession.hpp"
#include "engine/edit/InkMetrics.hpp"
#include "engine/edit/ScanFont.hpp"
#include "engine/edit/ScanPen.hpp"
#include "engine/edit/ScanRules.hpp"
#include "engine/render/PdfRenderer.hpp"
#include "ui/edit/EditOpener.hpp"

#include <QByteArray>
#include <QDataStream>
#include <QHash>
#include <QIODevice>
#include <QImage>
#include <QPainter>
#include <QRect>

#include <algorithm>
#include <vector>
#include <cmath>

inline constexpr qreal kSampleScale = 3.0;
inline constexpr double kScanAskewDeg = EditSession::kScanAskewDeg;

inline double distanceTo(const QRectF &r, const QPointF &p)
{
    const double dx = std::max({ 0.0, r.left() - p.x(), p.x() - r.right() });
    const double dy = std::max({ 0.0, r.top() - p.y(), p.y() - r.bottom() });
    return std::hypot(dx, dy);
}

// Where a line stands: the middle of where the words the fit found apart
// stand, taken back along the slope to the left edge of the page.
inline double scanStand(const ScanFont::Fit &fit, double slope)
{
    QList<double> stands;
    for (int j = 0; j < fit.wordBaselinesPt.size() && j < fit.wordMiddlesPt.size(); ++j)
        stands.append(fit.wordBaselinesPt.at(j) - slope * fit.wordMiddlesPt.at(j));
    if (stands.isEmpty()) return fit.originPt.y() - slope * fit.originPt.x();
    std::sort(stands.begin(), stands.end());
    return stands.at(stands.size() / 2);
}

// How far the lines of a scan slope, as the tangent: the median of the
// lines long enough to tell, weighted by their length.
inline double scanSlope(const QList<ScanFont::Fit> &fits)
{
    QList<std::pair<double, double>> slopes;
    double total = 0.0;
    for (const ScanFont::Fit &fit : fits)
        if (fit.sizePt > 0.0 && fit.slopeReachPt > 0.0) {
            slopes.append({ fit.slope, fit.slopeReachPt });
            total += fit.slopeReachPt;
        }
    if (slopes.isEmpty()) return 0.0;
    std::sort(slopes.begin(), slopes.end());
    double seen = 0.0;
    for (const auto &[slope, reach] : std::as_const(slopes))
        if ((seen += reach) >= total / 2.0) return slope;
    return slopes.last().first;
}

struct EditOpener::EditOpen : EditTarget
{

    int      page  { -1 };
    QSizeF   pageSize;

    ContentItem       contentItem;

    bool              paragraphBounds { false };

    bool                measurable { false };
    InkMetrics::FontInk probeInk;
    QPointF             clickPt;
    ScanFont::Match     scanFace;
    bool                ruleOnly { false };
    double              scanSlope { 0.0 };
    QList<double>       scanStands;
    QList<QList<std::pair<double, double>>> scanWords;

    PdfRenderer *renderer { nullptr };
    QHash<QByteArray, ScanFont::Match> *faces { nullptr };

    bool canSample() const { return renderer && renderer->backend() && block.page >= 0; }

    QColor sampleTextColor(const QRectF &pt)
    {
        const QRect px = pixelsOf(pt);
        return cover(px) ? InkMetrics::sampleTextColor(m_samp, px.translated(-m_area.topLeft()))
                         : InkMetrics::sampleTextColor(QImage(), px);
    }

    QColor sampleBackgroundColor(const QRectF &pt)
    {
        const QRect px = pixelsOf(pt);
        return cover(px) ? InkMetrics::sampleBackgroundColor(m_samp,
                                                             px.translated(-m_area.topLeft()))
                         : QColor();
    }

    InkMetrics::MeasuredInk measuredInk(const QRectF &pt)
    {
        const qreal pad = qMax(4.0, pt.height());
        if (!cover(pixelsOf(pt.adjusted(-pad, -pad, pad, pad)))) return {};
        const QPointF origin = QPointF(m_area.topLeft()) / kSampleScale;
        InkMetrics::MeasuredInk ink =
            InkMetrics::measuredInkPt(m_samp, pt.translated(-origin), kSampleScale);
        if (ink.height > 0.0) {
            ink.top  += origin.y();
            ink.left += origin.x();
        }
        return ink;
    }

    QRectF inkAround(const QRectF &pt)
    {
        const double pad = pt.height() * 0.7;
        if (!cover(pixelsOf(pt.adjusted(-pad, -pad, pad, pad)))) return pt;
        const QPointF origin = QPointF(m_area.topLeft()) / kSampleScale;
        return InkMetrics::inkAroundPt(m_samp, pt.translated(-origin), kSampleScale)
            .translated(origin);
    }

    QRectF inkOnBaseline(const QRectF &rect, double baseline)
    {
        const double pad = rect.height() * 0.7;
        if (!cover(pixelsOf(rect.adjusted(-pad, -pad, pad, pad)))) return {};
        const QPointF origin = QPointF(m_area.topLeft()) / kSampleScale;
        const QRectF ink = InkMetrics::inkOnBaselinePt(m_samp, rect.translated(-origin), baseline - origin.y(), kSampleScale);
        return ink.isEmpty() ? ink : ink.translated(origin);
    }

    bool ruledBetween(const QRectF &above, const QRectF &below)
    {
        if (!cover(pixelsOf(above.united(below)))) return false;
        // One of the rules found lies between the two lines along most of where they stand one above the other.
        double left = qMax(above.left(), below.left()), right = qMin(above.right(), below.right());
        if (right - left < qMin(above.width(), below.width()) * 0.5) {
            const QRectF &narrow = above.width() < below.width() ? above : below;
            left = narrow.left();
            right = narrow.right();
        }
        const QRect between = pixelsOf(QRectF(QPointF(left, above.center().y()), QPointF(right, below.center().y())));
        std::vector<bool> ruled(size_t(qMax(0, between.width())), false);
        for (const QRect &r : std::as_const(m_rules)) {
            if (r.width() <= r.height() || r.top() <= between.top() || r.bottom() >= between.bottom()) continue;
            for (int x = qMax(r.left(), between.left()); x <= qMin(r.right(), between.right()); ++x) ruled[size_t(x - between.left())] = true;
        }
        if (between.width() >= 8 && std::count(ruled.cbegin(), ruled.cend(), true) >= between.width() * 0.6) return true;
        const QPointF origin = QPointF(m_area.topLeft()) / kSampleScale;
        return InkMetrics::ruledBetweenPt(m_samp, above.translated(-origin), below.translated(-origin), kSampleScale);
    }

    // Whether a form's rule runs through the middle of a line along most of it and hardly anything else stands there,
    // as where OCR read the rule as words.
    bool ruleAlone(const QRectF &line)
    {
        const QRect in = pixelsOf(line);
        if (in.width() < 8 || !cover(in)) return false;
        const int edge = in.height() / 5;
        std::vector<bool> ruled(size_t(in.width()), false);
        qsizetype rule = 0;
        for (const QRect &r : std::as_const(m_rules)) {
            if (r.width() <= r.height() || r.top() < in.top() + edge || r.bottom() > in.bottom() - edge) continue;
            for (int x = qMax(r.left(), in.left()); x <= qMin(r.right(), in.right()); ++x) {
                ruled[size_t(x - in.left())] = true;
                ++rule;
            }
        }
        if (std::count(ruled.cbegin(), ruled.cend(), true) < in.width() * 0.6) return false;
        // The rules are gone from the sample: what ink is left there is the line's own, and thin letters hold little.
        return inked(in) * 8 <= rule;
    }

    // Whether ink stands close before a line on its rows, as a smudge or a patch the line's figures follow.
    bool inkBefore(const QRectF &line, double reach)
    {
        const QRect in = pixelsOf(QRectF(line.left() - reach, line.top(), reach - line.height() * 0.15, line.height()));
        // A gray block left the sample and still stands there.
        return in.width() >= 2 && cover(in)
            && (inked(in) >= qMax(6, in.height() / 2)
                || std::any_of(m_patches.cbegin(), m_patches.cend(), [&in](const QRect &patch) { return patch.intersects(in); }));
    }

    // Whether letters stand at a place, as where a row OCR did not read was clicked: ink, with the rules gone.
    bool lettersAt(const QPointF &place, double high)
    {
        const QRect in = pixelsOf(QRectF(place.x() - high * 2.0, place.y() - high * 0.2, high * 4.0, high * 0.4));
        return cover(in) && inked(in) >= qMax(6, in.width() / 12);
    }

    // Scanned lines narrowed to the ink standing on their baselines: a text
    // layer's box may reach far into the line above.
    void narrowToInk(QList<TextLayout::OriginalLine> &lines)
    {
        for (TextLayout::OriginalLine &line : lines) {
            const double pad = line.rect.height() * 0.7;
            if (!cover(pixelsOf(line.rect.adjusted(-pad, -pad, pad, pad)))) continue;
            const QPointF origin = QPointF(m_area.topLeft()) / kSampleScale;
            const QRectF ink = InkMetrics::inkOnBaselinePt(m_samp, line.rect.translated(-origin),
                                                           line.baseline - origin.y(), kSampleScale);
            if (ink.isEmpty()) continue;
            const QRectF own = ink.translated(origin);
            line.rect = QRectF(QPointF(qMin(line.rect.left(), own.left()), own.top()),
                               QPointF(qMax(line.rect.right(), own.right()), own.bottom()));
        }
    }

    // A scanned line with room above and below: its descenders hold too little
    // ink for the ink around it, and the matcher takes the line's own band.
    ScanFont::Line scanLine(const QRectF &rect, const QString &text, const QList<double> &wordStarts = {},
                            double baseline = -1.0)
    {
        const double h = rect.height();
        ScanFont::Line line { inkAround(rect).united(rect.adjusted(0.0, -h * 0.5, 0.0, h * 0.6)), text, rect, wordStarts, baseline, pageSlope(), {} };
        if (renderer && renderer->backend())
            for (const QRectF &picture : renderer->backend()->scanPictures(block.page))
                if (picture.intersects(rect)) line.foreignPt.append(picture);
        return line;
    }

    ScanFont::Match scanFont(QList<ScanFont::Line> lines, QList<ScanFont::Line> context,
                             const QString &family = {})
    {
        // The same lines measure the same for as long as the page shows the same.
        QByteArray key;
        if (faces) {
            QDataStream stream(&key, QIODevice::WriteOnly);
            stream << block.page << family;
            for (const QList<ScanFont::Line> *list : { &lines, &context }) {
                stream << qint32(list->size());
                for (const ScanFont::Line &line : *list) stream << line.inkPt << line.text << line.textPt << line.wordStartsPt << line.basePt << line.tilt << line.foreignPt;
            }
            if (const auto known = faces->constFind(key); known != faces->cend()) return *known;
        }
        QRect need;
        for (const ScanFont::Line &line : std::as_const(lines)) need = need.united(pixelsOf(line.inkPt));
        for (const ScanFont::Line &line : std::as_const(context)) need = need.united(pixelsOf(line.inkPt));
        if (!cover(need)) return {};
        const QPointF origin = QPointF(m_area.topLeft()) / kSampleScale;
        for (QList<ScanFont::Line> *list : { &lines, &context })
            for (ScanFont::Line &line : *list) {
                line.inkPt.translate(-origin);
                line.textPt.translate(-origin);
                if (line.basePt >= 0.0) line.basePt -= origin.y();
                for (double &x : line.wordStartsPt) x -= origin.x();
            }
        ScanFont::Match m = ScanFont::match(m_samp, kSampleScale, lines, context, family, m_black);
        for (ScanFont::Fit &fit : m.lines) {
            fit.originPt += origin;
            for (double &x : fit.wordMiddlesPt) x += origin.x();
            for (double &y : fit.wordBaselinesPt) y += origin.y();
        }
        if (faces) faces->insert(key, m);
        return m;
    }

    // A line's pixels on the paper around them, scaled so its ink is the given
    // height in pixels, as OCR reads a line alone.
    QImage lineImage(const QRectF &pt, double inkPx, int *dpi, QRectF *covers = nullptr)
    {
        const QRectF ink = inkAround(pt);
        const double pad = pt.height() * 0.6;
        const QRect px = pixelsOf(ink.adjusted(-pad, -pad, pad, pad));
        if (!cover(px) || ink.height() <= 0.0) return {};
        const QRect at = px.translated(-m_area.topLeft()).intersected(m_samp.rect());
        if (at.isEmpty()) return {};
        const double up = inkPx / (ink.height() * kSampleScale);
        *dpi = qRound(72.0 * kSampleScale * up);
        if (covers) *covers = QRectF(QPointF(at.topLeft() + m_area.topLeft()) / kSampleScale, QSizeF(at.size()) / kSampleScale);
        return m_samp.copy(at).convertToFormat(QImage::Format_Grayscale8)
            .scaled(qMax(1, qRound(at.width() * up)), qMax(1, qRound(at.height() * up)),
                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    InkMetrics::Blob blob(const QRectF &pt)
    {
        if (!cover(pixelsOf(pt))) return {};
        const QPointF origin = QPointF(m_area.topLeft()) / kSampleScale;
        InkMetrics::Blob ink = InkMetrics::blobPt(m_samp, pt.translated(-origin), kSampleScale);
        ink.rect.translate(origin);
        return ink;
    }

    double gapMiddle(const QRectF &pt)
    {
        if (!cover(pixelsOf(pt))) return qQNaN();
        const QPointF origin = QPointF(m_area.topLeft()) / kSampleScale;
        const double x = InkMetrics::gapMiddlePt(m_samp, pt.translated(-origin), kSampleScale);
        return std::isnan(x) ? x : x + origin.x();
    }

    QList<std::pair<double, double>> gaps(const QRectF &pt, double slope = 0.0)
    {
        if (!cover(pixelsOf(pt))) return {};
        const QPointF origin = QPointF(m_area.topLeft()) / kSampleScale;
        QList<std::pair<double, double>> gaps = InkMetrics::gapsPt(m_samp, pt.translated(-origin), kSampleScale, slope);
        for (auto &[from, to] : gaps) { from += origin.x(); to += origin.x(); }
        return gaps;
    }

    // How far the page lies askew, told by the widest line read near the edit; nothing where none is wide enough to tell.
    double pageSlope()
    {
        if (m_sloped || !scanned || !canSample()) return m_slope;
        m_sloped = true;
        const QRectF near(0.0, block.pdfBounds.center().y() - 150.0, pageSize.width(), 300.0);
        QRectF widest;
        for (const TextLayout::OriginalLine &line : renderer->backend()->originalLines(block.page, near, erasedZones))
            if (line.rect.width() > widest.width() && line.rect.width() >= line.rect.height() * 12.0) widest = line.rect;
        if (!widest.isEmpty()) m_slope = inkSlope(widest.adjusted(0.0, -widest.height() * 0.3, 0.0, widest.height() * 0.3));
        return m_slope;
    }

    double inkSlope(const QRectF &pt)
    {
        if (!cover(pixelsOf(pt))) return 0.0;
        const QPointF origin = QPointF(m_area.topLeft()) / kSampleScale;
        return InkMetrics::slopePt(m_samp, pt.translated(-origin), kSampleScale);
    }

    double strokeWidth(const QRectF &pt)
    {
        if (!cover(pixelsOf(pt))) return 0.0;
        const QPointF origin = QPointF(m_area.topLeft()) / kSampleScale;
        return InkMetrics::strokeWidthPt(m_samp, pt.translated(-origin), kSampleScale);
    }

    // A form's lines, given in a picture's pixels that cover an area of the page, leave the sample for good.
    void wipe(const QList<QRect> &rules, const QRectF &covers, double perPt)
    {
        if (rules.isEmpty() || perPt <= 0.0 || !cover(pixelsOf(covers))) return;
        const QColor paper = InkMetrics::sampleBackgroundColor(m_samp, pixelsOf(covers).translated(-m_area.topLeft()));
        if (!paper.isValid()) return;
        for (const QRect &r : rules) {
            const QRect px = pixelsOf(QRectF(covers.left() + r.x() / perPt, covers.top() + r.y() / perPt, r.width() / perPt, r.height() / perPt));
            m_wiped.append({ r.width() > r.height() ? px.adjusted(0, -1, 0, 1) : px.adjusted(-1, 0, 1, 0), paper.rgb() });
        }
        wipeSample();
    }

private:
    static QRect pixelsOf(const QRectF &pt)
    {
        return QRectF(pt.topLeft() * kSampleScale, pt.size() * kSampleScale).toAlignedRect();
    }

    // Renders the unedited page around the parts that are sampled instead of
    // the whole page at sample scale, which on heavy pages took a second.
    bool cover(const QRect &px)
    {
        if (!canSample()) return false;
        const QRect pageRect(QPoint(), renderer->backend()->pixelSize(block.page, kSampleScale));
        const QRect need = px.intersected(pageRect);
        if (need.isEmpty() || (!m_samp.isNull() && m_area.contains(need))) {
            if (scanned && !m_ruled && !m_samp.isNull()) {
                m_samp = ScanPen::without(m_samp, kSampleScale);
                dropRules();
                dropPatches(renderer->backend()->scanPatches(block.page));
            }
            return !m_samp.isNull();
        }
        const int margin = qRound(24 * kSampleScale);
        const QRect area = (m_samp.isNull() ? need : m_area.united(need))
                               .adjusted(-margin, -margin, margin, margin).intersected(pageRect);
        const PdfBackend::AreaRender render =
            renderer->backend()->renderChanges(block.page, kSampleScale, nullptr, area, false);
        if (render.image.isNull()) return false;
        m_samp = render.image;
        m_area = render.pixels;
        m_ruled = false;
        if (scanned) {
            // A pen's strokes in another colour than the print are none of a scan's text either.
            m_samp = ScanPen::without(m_samp, kSampleScale);
            dropRules();
            dropPatches(renderer->backend()->scanPatches(block.page));
        }
        wipeSample();
        return true;
    }

    // A form's rules are none of a scan's text: they leave the sample, and where they lay still tells lines apart.
    void dropRules()
    {
        m_rules.clear();
        m_ruled = true;
        // The page's own black, taken before its rules go: what a two hundredth of the sample is darker than.
        {
            const QImage gray = m_samp.convertToFormat(QImage::Format_Grayscale8);
            qsizetype tones[256] = {}, seen = 0;
            for (int y = 0; y < gray.height(); ++y) {
                const uchar *row = gray.constScanLine(y);
                for (int x = 0; x < gray.width(); ++x) ++tones[row[x]];
            }
            m_black = 255;
            for (int t = 0; t < 256; ++t)
                if ((seen += tones[t]) * 200 >= qsizetype(gray.width()) * gray.height()) { m_black = t; break; }
        }
        const QColor paper = InkMetrics::sampleBackgroundColor(m_samp, m_samp.rect());
        const QList<QRect> found = ScanRules::find(m_samp, kSampleScale);
        QPainter p(&m_samp);
        for (const QRect &r : found) {
            m_rules.append(r.translated(m_area.topLeft()));
            if (paper.isValid()) p.fillRect(r.width() > r.height() ? r.adjusted(0, -1, 0, 1) : r.adjusted(-1, 0, 1, 0), paper);
        }
        p.end();
        // Nor is a long curve drawn by hand beside the text: it leaves the sample as the rules do.
        const QImage gray = m_samp.convertToFormat(QImage::Format_Grayscale8);
        const QImage clean = ScanPen::withoutCurves(gray, kSampleScale);
        if (!paper.isValid() || clean.constBits() == gray.constBits()) return;
        m_samp = m_samp.convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < gray.height(); ++y) {
            const uchar *was = gray.constScanLine(y), *now = clean.constScanLine(y);
            QRgb *row = reinterpret_cast<QRgb *>(m_samp.scanLine(y));
            for (int x = 0; x < gray.width(); ++x)
                if (was[x] != now[x]) row[x] = paper.rgb();
        }
    }

    // Nor is a picture without ink that lies over the scan, as an edit leaves where it cleared a word: it
    // would measure as letters.
    void dropPatches(const QList<QRectF> &patches)
    {
        m_patches.clear();
        if (patches.isEmpty() || m_samp.isNull()) return;
        const QColor paper = InkMetrics::sampleBackgroundColor(m_samp, m_samp.rect());
        QPainter p(&m_samp);
        for (const QRectF &patch : patches) {
            const QRect px = pixelsOf(patch);
            if (!px.intersects(m_area)) continue;
            m_patches.append(px);
            if (paper.isValid()) p.fillRect(px.translated(-m_area.topLeft()), paper);
        }
    }

    void wipeSample()
    {
        if (m_wiped.isEmpty() || m_samp.isNull()) return;
        QPainter p(&m_samp);
        for (const auto &[px, paper] : std::as_const(m_wiped)) p.fillRect(px.translated(-m_area.topLeft()), QColor::fromRgb(paper));
    }

    // How many pixels of some of the sample are ink, told from the paper there.
    qsizetype inked(const QRect &pagePx) const
    {
        const QRect at = pagePx.translated(-m_area.topLeft()).intersected(m_samp.rect());
        std::vector<int> tones;
        for (int y = at.top(); y <= at.bottom(); ++y)
            for (int x = at.left(); x <= at.right(); ++x) tones.push_back(qGray(m_samp.pixel(x, y)));
        if (tones.empty()) return 0;
        std::vector<int> sorted = tones;
        std::nth_element(sorted.begin(), sorted.begin() + qsizetype(sorted.size() * 9 / 10), sorted.end());
        const int dark = sorted[sorted.size() * 9 / 10] - 30;
        return std::count_if(tones.cbegin(), tones.cend(), [dark](int tone) { return tone < dark; });
    }

    QList<std::pair<QRect, QRgb>> m_wiped;
    QList<QRect> m_rules;
    QList<QRect> m_patches;
    bool m_ruled { false };
    int m_black { -1 };
    double m_slope { 0.0 };
    bool m_sloped { false };

    QImage m_samp;
    QRect  m_area;
};

#endif
