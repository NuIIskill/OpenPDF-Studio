#pragma once

#include <QImage>
#include <QSize>
#include <QSizeF>

#ifdef HAVE_PDF_RENDERING

class EditSession;
class PdfBackend;

/// Page rasterisation in the units the view thinks in.
///
/// Everything backend-specific now sits in PdfBackend; what is left here is the
/// conversion between zoom percentages, PDF points and pixels. It stays a type
/// of its own because half the view holds one — layout engine, image layer,
/// exporter — and because it is the only place that knows what "100 %" means.
///
/// Borrows the backend and stays valid across file changes: the backend swaps
/// documents behind it.
class PdfRenderer
{
public:
    static constexpr qreal kScreenDpi  = 96.0;
    static constexpr qreal kPtsPerInch = 72.0;

    // Scale factor: PDF points → screen pixels at given zoom %
    static qreal screenScale(int zoomPercent)
    {
        return (kScreenDpi / kPtsPerInch) * (zoomPercent / 100.0);
    }

    explicit PdfRenderer(PdfBackend *backend) : m_backend(backend) {}

    QSizeF pageSizePts(int page) const;
    QSize  pageDisplaySize(int page, int zoomPercent) const;
    QImage renderPage(int page, qreal scale,
                      const EditSession *session = nullptr) const;

private:
    PdfBackend *m_backend;
};

#endif // HAVE_PDF_RENDERING
