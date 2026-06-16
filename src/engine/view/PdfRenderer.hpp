#pragma once

#include <QImage>
#include <QSize>
#include <QSizeF>

#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#elif defined(HAVE_POPPLER)
#include <poppler/qt6/poppler-qt6.h>
#endif

#ifdef HAVE_PDF_RENDERING

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

#ifdef HAVE_QT_PDF
    explicit PdfRenderer(QPdfDocument *doc);
#else
    explicit PdfRenderer(Poppler::Document *doc);
#endif

    QSizeF pageSizePts(int page) const;
    QSize  pageDisplaySize(int page, int zoomPercent) const;
    QImage renderPage(int page, qreal scale) const;

private:
#ifdef HAVE_QT_PDF
    QPdfDocument *m_doc;
#else
    Poppler::Document *m_doc;
#endif
};

#endif // HAVE_PDF_RENDERING
