#pragma once

#ifdef HAVE_PDF_RENDERING

#include "engine/document/PdfBackend.hpp"
#include "engine/edit/ContentModel.hpp"
#include "engine/edit/InkMetrics.hpp"
#include "engine/edit/ScanFont.hpp"
#include "engine/render/PdfRenderer.hpp"
#include "ui/edit/EditOpener.hpp"

#include <QImage>
#include <QRect>

#include <algorithm>
#include <cmath>

inline constexpr qreal kSampleScale = 3.0;

inline double distanceTo(const QRectF &r, const QPointF &p)
{
    const double dx = std::max({ 0.0, r.left() - p.x(), p.x() - r.right() });
    const double dy = std::max({ 0.0, r.top() - p.y(), p.y() - r.bottom() });
    return std::hypot(dx, dy);
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

    PdfRenderer *renderer { nullptr };

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

    // A scanned line with room above and below: its descenders hold too little
    // ink for the ink around it, and the matcher takes the line's own band.
    ScanFont::Line scanLine(const QRectF &rect, const QString &text)
    {
        const double h = rect.height();
        return { inkAround(rect).united(rect.adjusted(0.0, -h * 0.5, 0.0, h * 0.6)), text, rect };
    }

    ScanFont::Match scanFont(QList<ScanFont::Line> lines, QList<ScanFont::Line> context,
                             const QString &family = {})
    {
        QRect need;
        for (const ScanFont::Line &line : std::as_const(lines)) need = need.united(pixelsOf(line.inkPt));
        for (const ScanFont::Line &line : std::as_const(context)) need = need.united(pixelsOf(line.inkPt));
        if (!cover(need)) return {};
        const QPointF origin = QPointF(m_area.topLeft()) / kSampleScale;
        for (ScanFont::Line &line : lines) { line.inkPt.translate(-origin); line.textPt.translate(-origin); }
        for (ScanFont::Line &line : context) { line.inkPt.translate(-origin); line.textPt.translate(-origin); }
        ScanFont::Match m = ScanFont::match(m_samp, kSampleScale, lines, context, family);
        for (ScanFont::Fit &fit : m.lines) fit.originPt += origin;
        return m;
    }

    // A line's pixels on the paper around them, scaled so its ink is the given
    // height in pixels, as OCR reads a line alone.
    QImage lineImage(const QRectF &pt, double inkPx, int *dpi)
    {
        const QRectF ink = inkAround(pt);
        const double pad = pt.height() * 0.6;
        const QRect px = pixelsOf(ink.adjusted(-pad, -pad, pad, pad));
        if (!cover(px) || ink.height() <= 0.0) return {};
        const QRect at = px.translated(-m_area.topLeft()).intersected(m_samp.rect());
        if (at.isEmpty()) return {};
        const double up = inkPx / (ink.height() * kSampleScale);
        *dpi = qRound(72.0 * kSampleScale * up);
        return m_samp.copy(at).convertToFormat(QImage::Format_Grayscale8)
            .scaled(qMax(1, qRound(at.width() * up)), qMax(1, qRound(at.height() * up)),
                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    double strokeWidth(const QRectF &pt)
    {
        if (!cover(pixelsOf(pt))) return 0.0;
        const QPointF origin = QPointF(m_area.topLeft()) / kSampleScale;
        return InkMetrics::strokeWidthPt(m_samp, pt.translated(-origin), kSampleScale);
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
        if (need.isEmpty() || (!m_samp.isNull() && m_area.contains(need))) return !m_samp.isNull();
        const int margin = qRound(24 * kSampleScale);
        const QRect area = (m_samp.isNull() ? need : m_area.united(need))
                               .adjusted(-margin, -margin, margin, margin).intersected(pageRect);
        const PdfBackend::AreaRender render =
            renderer->backend()->renderChanges(block.page, kSampleScale, nullptr, area, false);
        if (render.image.isNull()) return false;
        m_samp = render.image;
        m_area = render.pixels;
        return true;
    }

    QImage m_samp;
    QRect  m_area;
};

#endif
