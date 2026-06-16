#ifdef HAVE_QT_PDF

#include "PdfRenderer.hpp"

PdfRenderer::PdfRenderer(QPdfDocument *doc)
    : m_doc(doc)
{
}

QSizeF PdfRenderer::pageSizePts(int page) const
{
    return m_doc->pagePointSize(page);
}

QSize PdfRenderer::pageDisplaySize(int page, int zoomPercent) const
{
    const qreal scale = screenScale(zoomPercent);
    const QSizeF pts  = pageSizePts(page);
    return QSize(static_cast<int>(pts.width() * scale),
                 static_cast<int>(pts.height() * scale));
}

QImage PdfRenderer::renderPage(int page, qreal scale) const
{
    const QSizeF pts = pageSizePts(page);
    const QSize  px(static_cast<int>(pts.width()  * scale),
                    static_cast<int>(pts.height() * scale));
    return m_doc->render(page, px);
}

#endif // HAVE_QT_PDF
