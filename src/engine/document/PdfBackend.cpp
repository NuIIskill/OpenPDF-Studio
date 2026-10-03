#include "engine/document/PdfBackend.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/edit/EditSession.hpp"

#ifdef HAVE_PDFIUM
#  include "engine/document/PdfiumBackend.hpp"
#endif

std::unique_ptr<PdfBackend> PdfBackend::create()
{

#ifdef HAVE_PDFIUM
    return std::make_unique<PdfiumBackend>();
#else
    return nullptr;
#endif
}

QImage PdfBackend::renderPage(int page, qreal scale, const EditSession *session) const
{
    QImage img = renderPage(page, scale);
    if (session && !img.isNull()) session->applyToImage(page, img, scale);
    return img;
}

PdfBackend::AreaRender PdfBackend::renderChanges(int page, qreal scale,
                                                 const EditSession *session,
                                                 const QRect &, bool) const
{
    AreaRender result;
    result.image   = renderPage(page, scale, session);
    result.pixels  = result.image.rect();
    result.changed = result.image.rect();
    return result;
}

QString PdfBackend::embeddedFontFamily(int, const QPointF &) const
{
    return {};
}

double PdfBackend::textWidthPt(int, const QPointF &, const QString &, double) const
{
    return -1.0;
}

double PdfBackend::standardTextWidthPt(const QString &, bool, bool,
                                       const QString &, double) const
{
    return -1.0;
}

bool PdfBackend::canEmbedFont(const QString &, bool, bool) const
{
    return false;
}

TextLayout::Metrics PdfBackend::editMetrics(const EditSession::Edit &) const
{
    return {};
}

QList<TextLayout::OriginalLine> PdfBackend::originalLines(int, const QRectF &,
                                                          const QList<QRectF> &) const
{
    return {};
}

#endif
