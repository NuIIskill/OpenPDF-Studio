#pragma once

#include <QImage>
#include <QSize>
#include <QSizeF>

#ifdef HAVE_PDF_RENDERING

class EditSession;
class PdfBackend;

/// Page rasterisation in the units the view thinks in.
class PdfRenderer
{
public:
    static constexpr qreal kScreenDpi  = 96.0;
    static constexpr qreal kPtsPerInch = 72.0;

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

#endif
