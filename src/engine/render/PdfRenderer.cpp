#include "engine/render/PdfRenderer.hpp"

#ifdef HAVE_QT_PDF

PdfRenderer::PdfRenderer(QPdfDocument *doc)
    : m_doc(doc)
{}

QSizeF PdfRenderer::pageSizePts(int page) const
{
    return m_doc->pagePointSize(page);
}

QSize PdfRenderer::pageDisplaySize(int page, int zoomPercent) const
{
    const qreal scale = screenScale(zoomPercent);
    const QSizeF pts  = pageSizePts(page);
    // Round, don't truncate: the page widget is sized from this while the
    // pixmap comes out of renderPage(), and a size one pixel apart makes
    // QLabel centre the pixmap and clip a row off the page.
    return QSize(qRound(pts.width() * scale), qRound(pts.height() * scale));
}

QImage PdfRenderer::renderPage(int page, qreal scale) const
{
    const QSizeF pts = pageSizePts(page);
    const QSize  px(qRound(pts.width()  * scale),
                    qRound(pts.height() * scale));
    return m_doc->render(page, px);
}

#elif defined(HAVE_POPPLER)

#include <QDebug>

#include <cmath>

PdfRenderer::PdfRenderer(Poppler::Document *doc)
    : m_doc(doc)
{}

QSizeF PdfRenderer::pageSizePts(int page) const
{
    auto p = m_doc->page(page);
    return p ? p->pageSizeF() : QSizeF();
}

QSize PdfRenderer::pageDisplaySize(int page, int zoomPercent) const
{
    const qreal scale = screenScale(zoomPercent);
    const QSizeF pts  = pageSizePts(page);
    // Poppler rounds the bitmap size UP (797 px for 796.8): truncating here
    // left the page widget a pixel short of its own pixmap, and QLabel centres
    // a pixmap that does not fit and clips a row off the page.
    return QSize(static_cast<int>(std::ceil(pts.width()  * scale)),
                 static_cast<int>(std::ceil(pts.height() * scale)));
}

QImage PdfRenderer::renderPage(int page, qreal scale) const
{
    // Poppler can throw (e.g. std::bad_optional_access from the win32 font
    // lookup when no substitute font is installed). A failed render must
    // degrade to an empty page, never terminate the app.
    try {
        auto p = m_doc->page(page);
        if (!p) return {};
        // Poppler takes DPI: pixels_per_inch = scale * 72 (since 1 pt = 1/72 inch)
        const double dpi = scale * kPtsPerInch;
        return p->renderToImage(dpi, dpi);
    } catch (const std::exception &ex) {
        qWarning() << "[Poppler] renderToImage failed for page" << page
                   << ":" << ex.what();
        return {};
    } catch (...) {
        return {};
    }
}

#endif // HAVE_QT_PDF / HAVE_POPPLER
