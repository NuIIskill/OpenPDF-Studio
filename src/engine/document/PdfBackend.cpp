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

bool PdfBackend::exactFontAt(int, const QPointF &) const
{
    return true;
}

QString PdfBackend::embeddedFontFamily(int, const QPointF &) const
{
    return {};
}

bool PdfBackend::hiddenText(int, const QRectF &) const
{
    return false;
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

QList<TextLayout::OriginalLine> PdfBackend::editLines(const EditSession::Edit &,
                                                     QList<QList<TextLayout::Source>> *,
                                                     QList<QList<TextLayout::Mark>> *) const
{
    return {};
}

QImage PdfBackend::renderEditText(int, qreal, const QList<EditSession::Edit> &, const QRect &) const
{
    return {};
}

QList<TextLayout::OriginalLine> PdfBackend::originalLines(int, const QRectF &,
                                                          const QList<QRectF> &) const
{
    return {};
}

#endif
