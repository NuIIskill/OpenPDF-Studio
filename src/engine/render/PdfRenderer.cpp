#include "engine/render/PdfRenderer.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/document/PdfBackend.hpp"

QSizeF PdfRenderer::pageSizePts(int page) const
{
    return m_backend ? m_backend->pageSizePts(page) : QSizeF();
}

QSize PdfRenderer::pageDisplaySize(int page, int zoomPercent) const
{

    return m_backend ? m_backend->pixelSize(page, screenScale(zoomPercent))
                     : QSize();
}

QImage PdfRenderer::renderPage(int page, qreal scale, const EditSession *session) const
{
    return m_backend ? m_backend->renderPage(page, scale, session) : QImage();
}

#endif
