#include "engine/document/PdfBackend.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/edit/EditSession.hpp"

#ifdef HAVE_PDFIUM
#  include "engine/document/PdfiumBackend.hpp"
#endif

std::unique_ptr<PdfBackend> PdfBackend::create()
{
    // Es gibt genau ein Backend. Qt6::Pdf und Poppler standen bis Schritt 10
    // daneben, damit sich beide gegen PDFium messen ließen; sie haben dabei
    // keine Aufgabe erfüllt, die PDFium nicht besser erfüllt, und sind
    // deshalb draußen. Die Laufzeitwahl über OPENPDF_BACKEND entfällt mit
    // ihnen — es gibt nichts mehr zu wählen.
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

QString PdfBackend::embeddedFontFamily(int, const QPointF &) const
{
    return {};
}

double PdfBackend::textWidthPt(int, const QPointF &, const QString &, double) const
{
    return -1.0;
}

#endif // HAVE_PDF_RENDERING
