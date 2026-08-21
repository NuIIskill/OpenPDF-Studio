#include "engine/render/PdfRenderer.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/document/PdfBackend.hpp"

QSizeF PdfRenderer::pageSizePts(int page) const
{
    return m_backend ? m_backend->pageSizePts(page) : QSizeF();
}

QSize PdfRenderer::pageDisplaySize(int page, int zoomPercent) const
{
    // The backend rounds, not this: it is the one whose renderPage() decides
    // how many pixels come out, and a page widget one pixel off from its own
    // pixmap makes QLabel centre and clip it.
    return m_backend ? m_backend->pixelSize(page, screenScale(zoomPercent))
                     : QSize();
}

QImage PdfRenderer::renderPage(int page, qreal scale) const
{
    return m_backend ? m_backend->renderPage(page, scale) : QImage();
}

#endif // HAVE_PDF_RENDERING
