#include "PdfRenderer.hpp"

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

#elif defined(HAVE_POPPLER)

#include <QDebug>

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
    return QSize(static_cast<int>(pts.width() * scale),
                 static_cast<int>(pts.height() * scale));
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
