#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "fpdf_edit.h"
#include "fpdfview.h"

#include <QPointF>
#include <QtGlobal>

#include <cmath>
#include <QRectF>
#include <QTransform>

/// The page as it is shown (top-left origin, y down, cropped and turned) against PDF user space.
class PdfiumPageSpace
{
public:
    explicit PdfiumPageSpace(FPDF_PAGE page)
    {
        FS_RECTF box { 0.f, 0.f, 0.f, 0.f };
        if (!page || !FPDF_GetPageBoundingBox(page, &box)) {
            box.top   = page ? FPDF_GetPageHeightF(page) : 0.f;
            box.right = page ? FPDF_GetPageWidthF(page) : 0.f;
        }
        const double left = qMin(box.left, box.right), right = qMax(box.left, box.right);
        const double bottom = qMin(box.bottom, box.top), top = qMax(box.bottom, box.top);
        const double width = right - left, height = top - bottom;
        m_turns = page ? ((FPDFPage_GetRotation(page) % 4) + 4) % 4 : 0;
        const QTransform unturned(1, 0, 0, -1, -left, top);
        switch (m_turns) {
        case 1:  m_toView = unturned * QTransform(0, 1, -1, 0, height, 0); break;
        case 2:  m_toView = unturned * QTransform(-1, 0, 0, -1, width, height); break;
        case 3:  m_toView = unturned * QTransform(0, -1, 1, 0, 0, width); break;
        default: m_toView = unturned; break;
        }
        m_toPdf = m_toView.inverted();
    }

    QPointF toView(double x, double y) const { return m_toView.map(QPointF(x, y)); }
    const QTransform &viewTransform() const { return m_toView; }
    QPointF toPdf(const QPointF &view) const { return m_toPdf.map(view); }

    QRectF toView(double left, double bottom, double right, double top) const
    {
        return m_toView.mapRect(QRectF(QPointF(left, bottom), QPointF(right, top)).normalized());
    }

    QRectF toPdf(const QRectF &view) const { return m_toPdf.mapRect(view.normalized()); }

    QPointF rightward() const { return linear(m_toPdf, QPointF(1, 0)); }
    QPointF upward() const { return linear(m_toPdf, QPointF(0, -1)); }

    QPointF seen(double x, double y) const
    {
        const QPointF v = linear(m_toView, QPointF(x, y));
        return QPointF(v.x(), -v.y());
    }

    int quarterTurns() const { return m_turns; }

    bool upright(double charAngle) const
    {
        if (charAngle < 0.0) return true;
        constexpr double kTurn = 6.283185307179586;
        // PDFium measures the angle clockwise, the way the page turns.
        double shown = std::fmod(charAngle + m_turns * kTurn / 4.0, kTurn);
        if (shown < 0.0) shown += kTurn;
        return shown < 0.01 || shown > kTurn - 0.01;
    }

private:
    static QPointF linear(const QTransform &t, const QPointF &v)
    {
        return t.map(v) - t.map(QPointF(0, 0));
    }

    QTransform m_toView;
    QTransform m_toPdf;
    int        m_turns { 0 };
};

#endif
