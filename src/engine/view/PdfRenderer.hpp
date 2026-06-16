#pragma once

#include <QImage>
#include <QSize>
#include <QSizeF>

#ifdef HAVE_QT_PDF
#include <QPdfDocument>

class PdfRenderer
{
public:
    static constexpr qreal kScreenDpi  = 96.0;
    static constexpr qreal kPtsPerInch = 72.0;

    explicit PdfRenderer(QPdfDocument *doc);

    // Scale factor: PDF points → screen pixels at given zoom %
    static qreal screenScale(int zoomPercent)
    {
        return (kScreenDpi / kPtsPerInch) * (zoomPercent / 100.0);
    }

    QSizeF pageSizePts(int page) const;
    QSize  pageDisplaySize(int page, int zoomPercent) const;
    QImage renderPage(int page, qreal scale) const;

private:
    QPdfDocument *m_doc;
};

#endif // HAVE_QT_PDF
