#include "engine/document/PdfiumPart.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "fpdf_edit.h"

#include <QList>

namespace {

constexpr int kMaxDepth = 8;

// How often PDFium halves an image of some size before it draws it into a bitmap of another.
int halvings(const QSize &image, const QSize &bitmap)
{
    const int least = qMin(image.width() / qMax(1, bitmap.width()), image.height() / qMax(1, bitmap.height()));
    int times = 0;
    while (times < 30 && (2 << times) <= least) ++times;
    return times;
}

void imageSizes(FPDF_PAGE page, FPDF_PAGEOBJECT form, int depth, QList<QSize> &out)
{
    const int count = form ? FPDFFormObj_CountObjects(form) : FPDFPage_CountObjects(page);
    for (int i = 0; i < count; ++i) {
        FPDF_PAGEOBJECT object = form ? FPDFFormObj_GetObject(form, static_cast<unsigned long>(i))
                                      : FPDFPage_GetObject(page, i);
        const int type = object ? FPDFPageObj_GetType(object) : FPDF_PAGEOBJ_UNKNOWN;
        unsigned int w = 0, h = 0;
        if (type == FPDF_PAGEOBJ_FORM && depth < kMaxDepth) imageSizes(page, object, depth + 1, out);
        else if (type == FPDF_PAGEOBJ_IMAGE && FPDFImageObj_GetImagePixelSize(object, &w, &h) && w > 1 && h > 1)
            out.append(QSize(int(w), int(h)));
    }
}

}

PdfiumPart::Frame PdfiumPart::frameFor(FPDF_PAGE page, const QRect &part, const QSize &pagePx)
{
    // PDFium reads an image the coarser the smaller the bitmap it renders into, whatever part of the page
    // that bitmap shows: halved for as long as the image stays twice as large as the bitmap both across and
    // down, a JPEG down to an eighth, so a part rendered into a bitmap of its own size shows a scan as a
    // mosaic. The bitmap is made wider or higher than the part until each image is halved as often as for
    // the whole page. An image drawn enlarged also differs in the pixels at the bitmap's edge, so the
    // bitmap starts and ends a little outside the part.
    constexpr int kEdge = 2;
    const QRect sheet(QPoint(), pagePx);
    Frame frame { part, QPoint() };
    if (!page || part.isEmpty() || pagePx.isEmpty() || part.contains(sheet)) return frame;
    frame.area = part.adjusted(-kEdge, -kEdge, kEdge, kEdge).intersected(sheet);
    frame.part = part.topLeft() - frame.area.topLeft();
    QSize bitmap = frame.area.size();
    QList<QSize> images;
    imageSizes(page, nullptr, 0, images);
    for (const QSize &image : std::as_const(images)) {
        const int whole = halvings(image, pagePx);
        if (halvings(image, bitmap) == whole) continue;
        // Either direction alone settles it; within the page one of them always does.
        const int wide = image.width() / (2 << whole) + 1, high = image.height() / (2 << whole) + 1;
        const bool across = wide <= pagePx.width(), down = high <= pagePx.height();
        const qint64 widened = qint64(qMax(bitmap.width(), wide)) * bitmap.height();
        const qint64 heightened = qint64(bitmap.width()) * qMax(bitmap.height(), high);
        if (across && (!down || widened <= heightened)) bitmap.setWidth(qMax(bitmap.width(), wide));
        else if (down) bitmap.setHeight(qMax(bitmap.height(), high));
        else bitmap = bitmap.expandedTo(pagePx);
    }
    frame.area.setSize(bitmap);
    return frame;
}

QImage PdfiumPart::cut(FPDF_BITMAP bitmap, const Frame &frame, const QSize &part)
{
    const auto *buffer = static_cast<const uchar *>(FPDFBitmap_GetBuffer(bitmap));
    if (!buffer || FPDFBitmap_GetFormat(bitmap) == FPDFBitmap_Gray || FPDFBitmap_GetFormat(bitmap) == FPDFBitmap_BGR) return {};
    const int stride = FPDFBitmap_GetStride(bitmap);
    const QImage::Format format = FPDFBitmap_GetFormat(bitmap) == FPDFBitmap_BGRA ? QImage::Format_ARGB32 : QImage::Format_RGB32;
    return QImage(buffer + qsizetype(frame.part.y()) * stride + qsizetype(frame.part.x()) * 4, part.width(), part.height(),
                  stride, format).copy();
}

#endif
