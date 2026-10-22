#include "engine/document/PdfiumImages.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumPageSpace.hpp"

#include "fpdf_edit.h"
#include "fpdf_transformpage.h"

#include <QTransform>

#include <QPair>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

namespace PdfiumImages {

namespace {

constexpr int    kMaxDepth = 8;
constexpr double kMinSide  = 4.0;
constexpr double kMaxPixels = 4096.0;

QTransform matrixOf(FPDF_PAGEOBJECT obj)
{
    FS_MATRIX m {};
    if (!FPDFPageObj_GetMatrix(obj, &m)) return {};
    return QTransform(m.a, m.b, m.c, m.d, m.e, m.f);
}

int childCount(FPDF_PAGE page, FPDF_PAGEOBJECT form)
{
    return form ? FPDFFormObj_CountObjects(form) : FPDFPage_CountObjects(page);
}

FPDF_PAGEOBJECT childAt(FPDF_PAGE page, FPDF_PAGEOBJECT form, int i)
{
    return form ? FPDFFormObj_GetObject(form, static_cast<unsigned long>(i))
                : FPDFPage_GetObject(page, i);
}

// The box of the object's clip path, in the space its bounds are given in.
std::optional<QRectF> clipBox(FPDF_PAGEOBJECT obj)
{
    FPDF_CLIPPATH clip = FPDFPageObj_GetClipPath(obj);
    if (!clip) return std::nullopt;
    std::optional<QRectF> box;
    for (int p = 0, paths = FPDFClipPath_CountPaths(clip); p < paths; ++p) {
        double left = 0, right = 0, bottom = 0, top = 0;
        bool any = false;
        for (int s = 0, n = FPDFClipPath_CountPathSegments(clip, p); s < n; ++s) {
            float x = 0, y = 0;
            if (!FPDFPathSegment_GetPoint(FPDFClipPath_GetPathSegment(clip, p, s), &x, &y))
                continue;
            if (!any) { left = right = x; bottom = top = y; any = true; continue; }
            left = qMin(left, double(x)); right = qMax(right, double(x));
            bottom = qMin(bottom, double(y)); top = qMax(top, double(y));
        }
        if (!any) continue;
        const QRectF path(QPointF(left, bottom), QPointF(right, top));
        box = box ? box->intersected(path) : path;
    }
    return box;
}

QRectF boundsOf(FPDF_PAGEOBJECT obj)
{
    float left = 0, bottom = 0, right = 0, top = 0;
    if (!FPDFPageObj_GetBounds(obj, &left, &bottom, &right, &top)) return {};
    return QRectF(QPointF(left, bottom), QPointF(right, top)).normalized();
}

QRectF clipped(const QRectF &box, const std::optional<QRectF> &clip)
{
    return clip ? box.intersected(*clip) : box;
}

QRectF toView(const PdfiumPageSpace &space, const QRectF &pdf)
{
    return space.toView(pdf.left(), pdf.top(), pdf.right(), pdf.bottom());
}

void walk(FPDF_PAGE page, FPDF_PAGEOBJECT form, const QTransform &toPage,
          const std::optional<QRectF> &outerClip, QList<int> &key,
          const PdfiumPageSpace &space, QList<Found> &out)
{
    for (int i = 0, n = childCount(page, form); i < n; ++i) {
        FPDF_PAGEOBJECT obj = childAt(page, form, i);
        FPDF_BOOL active = true;
        if (!obj || (FPDFPageObj_GetIsActive(obj, &active) && !active)) continue;
        const int type = FPDFPageObj_GetType(obj);
        if (type != FPDF_PAGEOBJ_IMAGE && type != FPDF_PAGEOBJ_FORM) continue;

        std::optional<QRectF> clip = outerClip;
        if (const auto own = clipBox(obj)) {
            const QRectF onPage = toPage.mapRect(*own);
            clip = clip ? clip->intersected(onPage) : onPage;
        }
        key.append(i);
        if (type == FPDF_PAGEOBJ_FORM) {
            if (key.size() < kMaxDepth)
                walk(page, obj, matrixOf(obj) * toPage, clip, key, space, out);
        } else {
            const QRectF full    = toPage.mapRect(boundsOf(obj));
            const QRectF visible = clipped(full, clip);
            if (visible.width() >= kMinSide && visible.height() >= kMinSide)
                out.append({ key, toView(space, visible), obj, toView(space, full) });
        }
        key.removeLast();
    }
}

QImage toImage(FPDF_BITMAP bitmap)
{
    const int w = FPDFBitmap_GetWidth(bitmap), h = FPDFBitmap_GetHeight(bitmap);
    const auto *buffer = static_cast<const uchar *>(FPDFBitmap_GetBuffer(bitmap));
    const int stride = FPDFBitmap_GetStride(bitmap);
    if (!buffer || w <= 0 || h <= 0) return {};
    switch (FPDFBitmap_GetFormat(bitmap)) {
    case FPDFBitmap_Gray: return QImage(buffer, w, h, stride, QImage::Format_Grayscale8).copy();
    case FPDFBitmap_BGR:  return QImage(buffer, w, h, stride, QImage::Format_BGR888).copy();
    case FPDFBitmap_BGRx: return QImage(buffer, w, h, stride, QImage::Format_RGB32).copy();
    case FPDFBitmap_BGRA: return QImage(buffer, w, h, stride, QImage::Format_ARGB32).copy();
    default:              return {};
    }
}

struct Chain {
    FPDF_PAGEOBJECT         object { nullptr };
    QList<FPDF_PAGEOBJECT>  forms;
};

Chain resolve(FPDF_PAGE page, const QList<int> &key)
{
    Chain chain;
    FPDF_PAGEOBJECT container = nullptr;
    for (int i : key) {
        if (container && FPDFPageObj_GetType(container) != FPDF_PAGEOBJ_FORM) return {};
        if (i < 0 || i >= childCount(page, container)) return {};
        if (container) chain.forms.append(container);
        container = childAt(page, container, i);
        if (!container) return {};
    }
    chain.object = container;
    return chain;
}

int indexOnPage(FPDF_PAGE page, FPDF_PAGEOBJECT obj)
{
    for (int i = 0, n = FPDFPage_CountObjects(page); i < n; ++i)
        if (FPDFPage_GetObject(page, i) == obj) return i;
    return -1;
}

void transform(FPDF_PAGEOBJECT obj, const QTransform &t)
{
    FPDFPageObj_Transform(obj, t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy());
    FPDFPageObj_TransformClipPath(obj, t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy());
}

// Sets the objects of a form placed on the page in its place, on this page
// only, so that one of them can change without the others.
void dissolve(FPDF_PAGE page, FPDF_PAGEOBJECT form, std::vector<FPDF_PAGEOBJECT> &removed)
{
    const int at = indexOnPage(page, form);
    if (at < 0) return;
    const QTransform placed = matrixOf(form);
    std::vector<FPDF_PAGEOBJECT> kids;
    for (int i = 0, n = FPDFFormObj_CountObjects(form); i < n; ++i)
        kids.push_back(FPDFFormObj_GetObject(form, static_cast<unsigned long>(i)));
    for (FPDF_PAGEOBJECT kid : kids) FPDFFormObj_RemoveObject(form, kid);
    if (FPDFPage_RemoveObject(page, form)) removed.push_back(form);
    int index = at;
    for (FPDF_PAGEOBJECT kid : kids) {
        transform(kid, placed);
        FPDFPage_InsertObjectAtIndex(page, kid, index++);
    }
}

bool hoist(FPDF_PAGE page, const Chain &chain, std::vector<FPDF_PAGEOBJECT> &removed)
{
    for (int round = 0; round <= chain.forms.size(); ++round) {
        if (indexOnPage(page, chain.object) >= 0) return true;
        for (FPDF_PAGEOBJECT form : chain.forms)
            if (indexOnPage(page, form) >= 0) { dissolve(page, form, removed); break; }
    }
    return indexOnPage(page, chain.object) >= 0;
}

QTransform moveOf(const EditSession::ImageChange &c, const PdfiumPageSpace &space)
{
    const int turns = ((c.quarterTurns % 4) + 4) % 4;
    const QSizeF turned = turns % 2 ? c.from.size().transposed() : c.from.size();
    if (turned.isEmpty()) return {};
    const QTransform inView = QTransform::fromTranslate(-c.from.center().x(), -c.from.center().y())
                            * QTransform().rotate(90.0 * turns)
                            * QTransform::fromScale(c.to.width() / turned.width(),
                                                    c.to.height() / turned.height())
                            * QTransform::fromTranslate(c.to.center().x(), c.to.center().y());
    return space.viewTransform() * inView * space.viewTransform().inverted();
}

FPDF_PAGEOBJECT newImage(FPDF_DOCUMENT doc, FPDF_PAGE page, const QImage &image,
                         const QRectF &to, const PdfiumPageSpace &space)
{
    const QImage src = image.convertToFormat(QImage::Format_ARGB32);
    FPDF_BITMAP bitmap = FPDFBitmap_Create(src.width(), src.height(), 1);
    if (!bitmap) return nullptr;
    auto *dst = static_cast<uchar *>(FPDFBitmap_GetBuffer(bitmap));
    const int stride = FPDFBitmap_GetStride(bitmap);
    for (int y = 0; y < src.height(); ++y)
        memcpy(dst + size_t(y) * size_t(stride), src.constScanLine(y), size_t(src.bytesPerLine()));
    FPDF_PAGEOBJECT obj = FPDFPageObj_NewImageObj(doc);
    if (obj) {
        FPDFImageObj_SetBitmap(&page, 1, obj, bitmap);
        const QPointF across = space.rightward() * to.width(), up = space.upward() * to.height();
        const QPointF origin = space.toPdf(to.bottomLeft());
        FPDFImageObj_SetMatrix(obj, across.x(), across.y(), up.x(), up.y(), origin.x(), origin.y());
    }
    FPDFBitmap_Destroy(bitmap);
    return obj;
}

}

QList<Found> find(FPDF_PAGE page)
{
    QList<Found> out;
    if (!page) return out;
    const PdfiumPageSpace space(page);
    QList<int> key;
    walk(page, nullptr, QTransform(), std::nullopt, key, space, out);
    return out;
}

QImage extract(FPDF_DOCUMENT doc, FPDF_PAGE page, const Found &found)
{
    FPDF_PAGEOBJECT obj = found.object;
    if (!doc || !page || !obj) return {};

    // Rendered at the image's own resolution rather than one pixel a point.
    FS_MATRIX m {};
    if (!FPDFPageObj_GetMatrix(obj, &m)) return {};
    unsigned int pw = 0, ph = 0;
    FPDFImageObj_GetImagePixelSize(obj, &pw, &ph);
    const double wPts = std::hypot(m.a, m.b), hPts = std::hypot(m.c, m.d);
    double k = 1.0;
    if (wPts > 0 && hPts > 0 && pw && ph) {
        k = qMax(pw / wPts, ph / hPts);
        k = qBound(1.0, k, kMaxPixels / qMax(wPts, hPts));
    }
    FS_MATRIX scaled = m;
    scaled.a *= k; scaled.b *= k; scaled.c *= k; scaled.d *= k;
    FPDFPageObj_SetMatrix(obj, &scaled);
    FPDF_BITMAP bitmap = FPDFImageObj_GetRenderedBitmap(doc, page, obj);
    FPDFPageObj_SetMatrix(obj, &m);
    if (!bitmap) return {};
    QImage image = toImage(bitmap);
    FPDFBitmap_Destroy(bitmap);
    if (image.isNull()) return image;

    const int turns = PdfiumPageSpace(page).quarterTurns();
    if (turns) image = image.transformed(QTransform().rotate(90.0 * turns));

    // Only the part the clip leaves visible.
    const QRectF &full = found.bounds;
    if (full.width() <= 0 || full.height() <= 0 || found.visible == full) return image;
    const qreal sx = image.width() / full.width(), sy = image.height() / full.height();
    const QRectF part((found.visible.left() - full.left()) * sx,
                      (found.visible.top()  - full.top())  * sy,
                      found.visible.width() * sx, found.visible.height() * sy);
    return image.copy(part.toAlignedRect().intersected(image.rect()));
}

void apply(FPDF_DOCUMENT doc, FPDF_PAGE page, const QList<EditSession::ImageChange> &changes,
           std::vector<FPDF_PAGEOBJECT> &removed)
{
    if (!doc || !page) return;
    const PdfiumPageSpace space(page);
    // Keys count the objects of the untouched page, so all are resolved first.
    QList<QPair<const EditSession::ImageChange *, Chain>> todo;
    for (const EditSession::ImageChange &c : changes) {
        const bool same = !c.removed && c.replacement.isNull() && c.quarterTurns % 4 == 0
                       && c.from == c.to;
        if (same) continue;
        const Chain chain = resolve(page, c.key);
        if (chain.object && FPDFPageObj_GetType(chain.object) == FPDF_PAGEOBJ_IMAGE)
            todo.append({ &c, chain });
    }
    for (const auto &[c, chain] : std::as_const(todo)) {
        if (!hoist(page, chain, removed)) continue;
        if (c->removed) {
            if (FPDFPage_RemoveObject(page, chain.object)) removed.push_back(chain.object);
        } else if (!c->replacement.isNull()) {
            FPDF_PAGEOBJECT fresh = newImage(doc, page, c->replacement, c->to, space);
            const int at = indexOnPage(page, chain.object);
            if (!fresh || !FPDFPage_InsertObjectAtIndex(page, fresh, at)) {
                if (fresh) FPDFPageObj_Destroy(fresh);
                continue;
            }
            if (FPDFPage_RemoveObject(page, chain.object)) removed.push_back(chain.object);
        } else {
            transform(chain.object, moveOf(*c, space));
        }
    }
}

}

#endif
