#include "engine/redact/PdfiumRedaction.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumPageSpace.hpp"
#include "engine/document/PdfiumText.hpp"
#include "engine/document/PdfiumTextCut.hpp"

#include "fpdf_annot.h"
#include "fpdf_edit.h"
#include "fpdf_text.h"
#include "fpdf_transformpage.h"

#include <QColor>
#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QSet>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace {

// Paths are cut at this many times their size, so the curves Qt flattens stay smooth.
constexpr double kFine        = 16.0;
constexpr double kRasterDpi   = 300.0;
constexpr double kRasterLimit = 60.0e6;

QRectF grown(const QRectF &box)
{
    return box.adjusted(-0.01, -0.01, 0.01, 0.01);
}

QRectF viewBounds(FPDF_PAGEOBJECT obj, const PdfiumPageSpace &space)
{
    float l = 0, b = 0, r = 0, t = 0;
    if (!FPDFPageObj_GetBounds(obj, &l, &b, &r, &t)) return {};
    return space.toView(l, b, r, t);
}

bool touches(const QList<QRectF> &areas, const QRectF &box)
{
    return std::any_of(areas.cbegin(), areas.cend(),
                       [&box](const QRectF &area) { return area.intersects(box); });
}

bool covered(const QList<QRectF> &areas, const QRectF &box)
{
    return std::any_of(areas.cbegin(), areas.cend(),
                       [&box](const QRectF &area) { return area.contains(box); });
}

QPainterPath areaPath(const QList<QRectF> &areas, const PdfiumPageSpace &space, double scale)
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    const QTransform toUser = space.viewTransform().inverted() * QTransform::fromScale(scale, scale);
    for (const QRectF &area : areas) path.addPolygon(toUser.map(QPolygonF(area)));
    return path;
}

std::optional<QPainterPath> segmentsPath(int count, const std::function<FPDF_PATHSEGMENT(int)> &segment)
{
    QPainterPath path;
    QList<QPointF> bezier;
    for (int i = 0; i < count; ++i) {
        FPDF_PATHSEGMENT s = segment(i);
        float x = 0, y = 0;
        if (!s || !FPDFPathSegment_GetPoint(s, &x, &y)) return std::nullopt;
        const QPointF p(x, y);
        switch (FPDFPathSegment_GetType(s)) {
        case FPDF_SEGMENT_MOVETO: path.moveTo(p); break;
        case FPDF_SEGMENT_LINETO: path.lineTo(p); break;
        case FPDF_SEGMENT_BEZIERTO:
            bezier.append(p);
            if (bezier.size() == 3) {
                path.cubicTo(bezier.at(0), bezier.at(1), bezier.at(2));
                bezier.clear();
            }
            break;
        default: return std::nullopt;
        }
        if (FPDFPathSegment_GetClose(s)) path.closeSubpath();
    }
    return path;
}

struct Ink {
    QPainterPath fill;
    QPainterPath stroke;
    QColor       fillColor;
    QColor       strokeColor;
};

// What a path paints, as areas in user space at kFine.
std::optional<Ink> inkOf(FPDF_PAGEOBJECT obj)
{
    const std::optional<QPainterPath> shape = segmentsPath(
        FPDFPath_CountSegments(obj), [obj](int i) { return FPDFPath_GetPathSegment(obj, i); });
    FS_MATRIX m {};
    int fillMode = FPDF_FILLMODE_NONE;
    FPDF_BOOL stroked = false;
    if (!shape || !FPDFPageObj_GetMatrix(obj, &m) || !FPDFPath_GetDrawMode(obj, &fillMode, &stroked))
        return std::nullopt;
    const QTransform matrix(m.a, m.b, m.c, m.d, m.e, m.f);
    const QTransform toUser = matrix * QTransform::fromScale(kFine, kFine);

    Ink ink;
    unsigned int r = 0, g = 0, b = 0, a = 0;
    if (fillMode != FPDF_FILLMODE_NONE) {
        if (!FPDFPageObj_GetFillColor(obj, &r, &g, &b, &a)) return std::nullopt;
        ink.fillColor = QColor(int(r), int(g), int(b), int(a));
        QPainterPath fill = *shape;
        fill.setFillRule(fillMode == FPDF_FILLMODE_ALTERNATE ? Qt::OddEvenFill : Qt::WindingFill);
        ink.fill = toUser.map(fill);
        ink.fill.setFillRule(fill.fillRule());
    }
    if (stroked) {
        if (!FPDFPageObj_GetStrokeColor(obj, &r, &g, &b, &a)) return std::nullopt;
        ink.strokeColor = QColor(int(r), int(g), int(b), int(a));
        float width = 0.f;
        FPDFPageObj_GetStrokeWidth(obj, &width);
        // A width of nought is the thinnest line a device shows.
        const double scale = std::sqrt(std::abs(matrix.determinant()));
        const double w = width > 0.f ? width : (scale > 0.0 ? 0.25 / scale : 0.25);
        QPainterPathStroker stroker;
        stroker.setWidth(w);
        switch (FPDFPageObj_GetLineCap(obj)) {
        case FPDF_LINECAP_ROUND:             stroker.setCapStyle(Qt::RoundCap); break;
        case FPDF_LINECAP_PROJECTING_SQUARE: stroker.setCapStyle(Qt::SquareCap); break;
        default:                             stroker.setCapStyle(Qt::FlatCap); break;
        }
        switch (FPDFPageObj_GetLineJoin(obj)) {
        case FPDF_LINEJOIN_ROUND: stroker.setJoinStyle(Qt::RoundJoin); break;
        case FPDF_LINEJOIN_BEVEL: stroker.setJoinStyle(Qt::BevelJoin); break;
        default:                  stroker.setJoinStyle(Qt::SvgMiterJoin); break;
        }
        stroker.setMiterLimit(10.0);
        const int dashes = FPDFPageObj_GetDashCount(obj);
        if (dashes > 0) {
            std::vector<float> dash(size_t(dashes), 0.f);
            float phase = 0.f;
            FPDFPageObj_GetDashArray(obj, dash.data(), size_t(dashes));
            FPDFPageObj_GetDashPhase(obj, &phase);
            QList<qreal> pattern;
            for (float d : dash) pattern.append(qMax(1e-3, double(d) / w));
            if (pattern.size() % 2) pattern += pattern;
            if (std::any_of(dash.cbegin(), dash.cend(), [](float d) { return d > 0.f; })) {
                stroker.setDashPattern(pattern);
                stroker.setDashOffset(phase / w);
            }
        }
        ink.stroke = toUser.map(stroker.createStroke(*shape));
        ink.stroke.setFillRule(Qt::WindingFill);
    }
    return ink;
}

// The clip a path is shown through, in user space at kFine; empty when it has none.
std::optional<QPainterPath> clipOf(FPDF_PAGEOBJECT obj, bool *clipped)
{
    *clipped = false;
    FPDF_CLIPPATH clip = FPDFPageObj_GetClipPath(obj);
    const int paths = clip ? FPDFClipPath_CountPaths(clip) : 0;
    QPainterPath region;
    for (int p = 0; p < paths; ++p) {
        const int count = FPDFClipPath_CountPathSegments(clip, p);
        if (count < 0) return std::nullopt;
        if (count == 0) continue;
        std::optional<QPainterPath> path = segmentsPath(
            count, [clip, p](int i) { return FPDFClipPath_GetPathSegment(clip, p, i); });
        if (!path) return std::nullopt;
        path->setFillRule(Qt::WindingFill);
        region = *clipped ? region.intersected(*path) : *path;
        *clipped = true;
    }
    return QTransform::fromScale(kFine, kFine).map(region);
}

FPDF_PAGEOBJECT toObject(const QPainterPath &shape, const QColor &color)
{
    FPDF_PAGEOBJECT obj = nullptr;
    const auto at = [&shape](int i) {
        const QPainterPath::Element e = shape.elementAt(i);
        return QPointF(e.x / kFine, e.y / kFine);
    };
    for (int i = 0; i < shape.elementCount(); ++i) {
        const QPainterPath::Element e = shape.elementAt(i);
        const QPointF p = at(i);
        if (e.isMoveTo()) {
            if (!obj) obj = FPDFPageObj_CreateNewPath(float(p.x()), float(p.y()));
            else      FPDFPath_MoveTo(obj, float(p.x()), float(p.y()));
        } else if (!obj) {
            return nullptr;
        } else if (e.isLineTo()) {
            FPDFPath_LineTo(obj, float(p.x()), float(p.y()));
        } else if (e.isCurveTo() && i + 2 < shape.elementCount()) {
            const QPointF c2 = at(i + 1), end = at(i + 2);
            FPDFPath_BezierTo(obj, float(p.x()), float(p.y()), float(c2.x()), float(c2.y()),
                              float(end.x()), float(end.y()));
            i += 2;
        }
    }
    if (!obj) return nullptr;
    FPDFPath_SetDrawMode(obj, shape.fillRule() == Qt::OddEvenFill ? FPDF_FILLMODE_ALTERNATE
                                                                   : FPDF_FILLMODE_WINDING, false);
    FPDFPageObj_SetFillColor(obj, uint(color.red()), uint(color.green()), uint(color.blue()),
                             uint(color.alpha()));
    return obj;
}

struct PathCut {
    FPDF_PAGEOBJECT        obj { nullptr };
    QList<FPDF_PAGEOBJECT> parts;
};

// What of a path lies outside the areas, as filled paths; a stroke becomes its outline.
bool cutPath(FPDF_PAGEOBJECT obj, const QPainterPath &area, PathCut &out)
{
    out.obj = obj;
    bool clipped = false;
    const std::optional<Ink> ink = inkOf(obj);
    const std::optional<QPainterPath> clip = clipOf(obj, &clipped);
    if (!ink || !clip) return false;
    const auto keep = [&](const QPainterPath &shape, const QColor &color) {
        if (shape.isEmpty()) return true;
        QPainterPath kept = shape.subtracted(area);
        if (clipped) kept = kept.intersected(*clip);
        if (kept.isEmpty()) return true;
        FPDF_PAGEOBJECT part = toObject(kept, color);
        if (part) out.parts.append(part);
        return part != nullptr;
    };
    return keep(ink->fill, ink->fillColor) && keep(ink->stroke, ink->strokeColor);
}

int bytesPerPixel(int format)
{
    switch (format) {
    case FPDFBitmap_Gray: return 1;
    case FPDFBitmap_BGR:  return 3;
    case FPDFBitmap_BGRx:
    case FPDFBitmap_BGRA:
    case FPDFBitmap_BGRA_Premul: return 4;
    default: return 0;
    }
}

bool plainImage(FPDF_PAGEOBJECT obj, FPDF_PAGE page)
{
    FPDF_IMAGEOBJ_METADATA meta {};
    if (!FPDFImageObj_GetImageMetadata(obj, page, &meta)) return false;
    return !FPDFPageObj_HasTransparency(obj) && meta.colorspace != FPDF_COLORSPACE_UNKNOWN
        && meta.colorspace != FPDF_COLORSPACE_PATTERN;
}

// The image pixels under the area, from user space through the image's matrix.
std::optional<QImage> imageMask(FPDF_PAGEOBJECT obj, int width, int height,
                                const QPainterPath &area, bool edges)
{
    FS_MATRIX m {};
    if (!FPDFPageObj_GetMatrix(obj, &m)) return std::nullopt;
    bool invertible = false;
    const QTransform toUnit = QTransform(m.a, m.b, m.c, m.d, m.e, m.f).inverted(&invertible);
    if (!invertible) return std::nullopt;
    QImage mask(width, height, QImage::Format_Grayscale8);
    mask.fill(0);
    QPainter painter(&mask);
    painter.setTransform(toUnit * QTransform(width, 0, 0, -height, 0, height));
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPath(area);
    if (edges) {
        // Pixels the edge only grazes are taken too.
        QPen edge(Qt::white, 2.0);
        edge.setCosmetic(true);
        painter.setPen(edge);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(area);
    }
    painter.end();
    return mask;
}

bool blackOut(FPDF_PAGE page, FPDF_PAGEOBJECT obj, const QPainterPath &area, bool *changed)
{
    *changed = false;
    FPDF_BITMAP bitmap = FPDFImageObj_GetBitmap(obj);
    if (!bitmap) return false;
    const int width = FPDFBitmap_GetWidth(bitmap), height = FPDFBitmap_GetHeight(bitmap);
    const int stride = FPDFBitmap_GetStride(bitmap);
    const int bpp = bytesPerPixel(FPDFBitmap_GetFormat(bitmap));
    const std::optional<QImage> mask = bpp > 0 ? imageMask(obj, width, height, area, true)
                                               : std::nullopt;
    bool ok = mask.has_value();
    if (ok) {
        auto *bits = static_cast<uchar *>(FPDFBitmap_GetBuffer(bitmap));
        const int channels = qMin(bpp, 3);
        for (int y = 0; y < height; ++y) {
            const uchar *in = mask->constScanLine(y);
            uchar *row = bits + qsizetype(y) * stride;
            for (int x = 0; x < width; ++x) {
                if (!in[x]) continue;
                std::fill_n(row + qsizetype(x) * bpp, channels, uchar(0));
                *changed = true;
            }
        }
        if (*changed) ok = FPDFImageObj_SetBitmap(&page, 1, obj, bitmap);
    }
    FPDFBitmap_Destroy(bitmap);
    return ok;
}

bool imageBlackUnder(FPDF_PAGEOBJECT obj, const QPainterPath &area)
{
    FPDF_BITMAP bitmap = FPDFImageObj_GetBitmap(obj);
    if (!bitmap) return false;
    const int width = FPDFBitmap_GetWidth(bitmap), height = FPDFBitmap_GetHeight(bitmap);
    const int stride = FPDFBitmap_GetStride(bitmap);
    const int bpp = bytesPerPixel(FPDFBitmap_GetFormat(bitmap));
    const std::optional<QImage> mask = bpp > 0 ? imageMask(obj, width, height, area, false)
                                               : std::nullopt;
    bool black = mask.has_value();
    const auto *bits = static_cast<const uchar *>(FPDFBitmap_GetBuffer(bitmap));
    for (int y = 0; black && y < height; ++y) {
        const uchar *in = mask->constScanLine(y);
        const uchar *row = bits + qsizetype(y) * stride;
        for (int x = 0; x < width && black; ++x) {
            if (!in[x]) continue;
            const uchar *px = row + qsizetype(x) * bpp;
            black = std::all_of(px, px + qMin(bpp, 3), [](uchar v) { return v == 0; });
        }
    }
    FPDFBitmap_Destroy(bitmap);
    return black;
}

QRectF annotationView(FPDF_ANNOTATION annot, const PdfiumPageSpace &space)
{
    FS_RECTF r {};
    if (!FPDFAnnot_GetRect(annot, &r)) return {};
    return space.toView(r.left, r.bottom, r.right, r.top);
}

// Annotations over the areas go, and with them the popups and replies that point at them.
void dropAnnotations(FPDF_PAGE page, const QList<QRectF> &areas, const PdfiumPageSpace &space)
{
    for (int i = FPDFPage_GetAnnotCount(page) - 1; i >= 0; --i) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
        if (!annot) continue;
        bool hit = touches(areas, grown(annotationView(annot, space)));
        for (const char *key : { "Parent", "IRT" }) {
            if (hit) break;
            if (FPDF_ANNOTATION linked = FPDFAnnot_GetLinkedAnnot(annot, key)) {
                hit = touches(areas, grown(annotationView(linked, space)));
                FPDFPage_CloseAnnot(linked);
            }
        }
        FPDFPage_CloseAnnot(annot);
        if (hit) FPDFPage_RemoveAnnot(page, i);
    }
}

void cover(FPDF_PAGE page, const QList<QRectF> &areas, const PdfiumPageSpace &space)
{
    for (const QRectF &area : areas) {
        const QRectF r = space.toPdf(area);
        FPDF_PAGEOBJECT bar = FPDFPageObj_CreateNewRect(float(r.left()), float(r.top()),
                                                        float(r.width()), float(r.height()));
        if (!bar) continue;
        FPDFPageObj_SetFillColor(bar, 0, 0, 0, 255);
        FPDFPath_SetDrawMode(bar, FPDF_FILLMODE_WINDING, false);
        FPDFPage_InsertObject(page, bar);
    }
}

QRectF shownPage(FPDF_PAGE page, const PdfiumPageSpace &space)
{
    FS_RECTF box {};
    if (!FPDF_GetPageBoundingBox(page, &box)) return {};
    return space.toView(box.left, box.bottom, box.right, box.top);
}

FPDF_BITMAP renderShown(FPDF_PAGE page, const QRectF &shown, double scale)
{
    const int width  = qMax(1, qRound(shown.width() * scale));
    const int height = qMax(1, qRound(shown.height() * scale));
    FPDF_BITMAP bitmap = FPDFBitmap_Create(width, height, 0);
    if (!bitmap) return nullptr;
    FPDFBitmap_FillRect(bitmap, 0, 0, width, height, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, width, height, 0, FPDF_ANNOT);
    return bitmap;
}

QRect pixelsOf(const QRectF &area, const QRectF &shown, FPDF_BITMAP bitmap)
{
    const double sx = FPDFBitmap_GetWidth(bitmap) / shown.width();
    const double sy = FPDFBitmap_GetHeight(bitmap) / shown.height();
    const QRectF px((area.left() - shown.left()) * sx, (area.top() - shown.top()) * sy,
                    area.width() * sx, area.height() * sy);
    return px.toAlignedRect() & QRect(0, 0, FPDFBitmap_GetWidth(bitmap), FPDFBitmap_GetHeight(bitmap));
}

// The page as one image of itself with the areas black, for what cannot be
// cut in place; the page is replaced by it when the copy is rebuilt.
bool rasterise(FPDF_PAGE page, const QList<QRectF> &areas, const PdfiumPageSpace &space,
               PdfiumRedaction::Raster *raster)
{
    const QRectF shown = shownPage(page, space);
    if (shown.isEmpty()) return false;
    double scale = kRasterDpi / 72.0;
    const double pixels = shown.width() * shown.height() * scale * scale;
    if (pixels > kRasterLimit) scale *= std::sqrt(kRasterLimit / pixels);
    FPDF_BITMAP bitmap = renderShown(page, shown, scale);
    if (!bitmap) return false;
    const int width = FPDFBitmap_GetWidth(bitmap), height = FPDFBitmap_GetHeight(bitmap);
    auto *bits = static_cast<uchar *>(FPDFBitmap_GetBuffer(bitmap));
    const int stride = FPDFBitmap_GetStride(bitmap);
    for (const QRectF &area : areas) {
        const QRect px = pixelsOf(area, shown, bitmap).adjusted(-1, -1, 1, 1) & QRect(0, 0, width, height);
        for (int y = px.top(); y <= px.bottom(); ++y)
            std::fill_n(bits + qsizetype(y) * stride + qsizetype(px.left()) * 4,
                        qsizetype(px.width()) * 4, uchar(0));
    }
    QByteArray rgb(qsizetype(width) * height * 3, Qt::Uninitialized);
    for (int y = 0; y < height; ++y) {
        const uchar *in = bits + qsizetype(y) * stride;
        uchar *row = reinterpret_cast<uchar *>(rgb.data()) + qsizetype(y) * width * 3;
        for (int x = 0; x < width; ++x) {
            row[x * 3]     = in[x * 4 + 2];
            row[x * 3 + 1] = in[x * 4 + 1];
            row[x * 3 + 2] = in[x * 4];
        }
    }
    FPDFBitmap_Destroy(bitmap);
    // qCompress puts the length in front of a zlib stream.
    raster->rgb  = qCompress(rgb, 6).mid(4);
    raster->size = QSize(width, height);
    const QPointF origin = space.toPdf(shown.bottomLeft());
    const QPointF across = space.toPdf(shown.bottomRight()) - origin;
    const QPointF up     = space.toPdf(shown.topLeft()) - origin;
    raster->placement = QTransform(across.x(), across.y(), up.x(), up.y(), origin.x(), origin.y());
    raster->bars.clear();
    for (const QRectF &area : areas) raster->bars.append(space.toPdf(area));
    return true;
}

// Text that shows nothing: it can go whole, wherever it reaches.
bool blank(FPDF_PAGEOBJECT obj, FPDF_TEXTPAGE textPage)
{
    const unsigned long bytes = FPDFTextObj_GetText(obj, textPage, nullptr, 0);
    if (bytes <= 2) return true;
    std::u16string buffer(bytes / 2, u'\0');
    FPDFTextObj_GetText(obj, textPage, reinterpret_cast<FPDF_WCHAR *>(buffer.data()), bytes);
    return QString::fromStdU16String(buffer).trimmed().remove(QChar(0)).isEmpty();
}

int indexOf(FPDF_PAGE page, FPDF_PAGEOBJECT obj)
{
    for (int i = 0, n = FPDFPage_CountObjects(page); i < n; ++i)
        if (FPDFPage_GetObject(page, i) == obj) return i;
    return -1;
}

}

bool PdfiumRedaction::apply(FPDF_DOCUMENT doc, FPDF_PAGE page, int pageIndex,
                            const QList<QRectF> &shownAreas, bool asImage, bool &inPlace,
                            Raster *raster)
{
    if (!doc || !page || shownAreas.isEmpty() || !raster) return false;
    if (asImage) {
        qInfo() << "[Redaction] page" << pageIndex + 1 << "is redacted as an image: asked for";
        return rasterise(page, shownAreas, PdfiumPageSpace(page), raster);
    }
    // Text is cut where it stands upright, which on a turned page is before the turn.
    const PdfiumPageSpace shown(page);
    const int turns = FPDFPage_GetRotation(page);
    FPDFPage_SetRotation(page, 0);
    const PdfiumPageSpace space(page);
    QList<QRectF> areas;
    for (const QRectF &area : shownAreas) {
        const QRectF r = shown.toPdf(area);
        areas.append(space.toView(r.left(), r.top(), r.right(), r.bottom()));
    }
    const QPainterPath userArea = areaPath(areas, space, 1.0);
    const QPainterPath fineArea = areaPath(areas, space, kFine);

    QList<FPDF_PAGEOBJECT> objects;
    for (int i = 0, n = FPDFPage_CountObjects(page); i < n; ++i)
        objects.append(FPDFPage_GetObject(page, i));
    const QSet<FPDF_PAGEOBJECT> top(objects.cbegin(), objects.cend());

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    PdfiumTextCut::Result text;
    if (textPage)
        text = PdfiumTextCut::cut(textPage, areas, space, PdfiumTextCut::Inside::Touching);
    const auto nested = [&top](const QList<FPDF_PAGEOBJECT> &list) {
        return std::any_of(list.cbegin(), list.cend(),
                           [&top](FPDF_PAGEOBJECT obj) { return !top.contains(obj); });
    };
    // Text inside a form and turned text cut in two cannot be taken out character by character.
    QString why = !textPage ? QStringLiteral("no text page")
                : !text.turned.isEmpty() ? QStringLiteral("turned text")
                : nested(text.cut) || nested(text.whole) ? QStringLiteral("text in a form")
                : QString();

    QList<FPDF_PAGEOBJECT> dropped = text.whole;
    QList<FPDF_PAGEOBJECT> images;
    std::vector<PathCut> paths;
    for (FPDF_PAGEOBJECT obj : std::as_const(objects)) {
        if (!why.isEmpty()) break;
        const QRectF box = viewBounds(obj, space);
        if (!touches(areas, grown(box))) continue;
        const int type = FPDFPageObj_GetType(obj);
        if (type == FPDF_PAGEOBJ_TEXT && text.seen.contains(obj)) continue;
        if (covered(areas, box) || (type == FPDF_PAGEOBJ_TEXT && blank(obj, textPage))) {
            dropped.append(obj);
        } else if (type == FPDF_PAGEOBJ_IMAGE && plainImage(obj, page)) {
            images.append(obj);
        } else if (type == FPDF_PAGEOBJ_PATH && !FPDFPageObj_HasTransparency(obj)) {
            PathCut cut;
            if (!cutPath(obj, fineArea, cut)) why = QStringLiteral("unreadable path");
            paths.push_back(cut);
        } else {
            why = QStringLiteral("object of type %1").arg(type);
        }
    }

    // Blacked pixels lie under the bars, so a page given up afterwards still looks the same.
    for (FPDF_PAGEOBJECT obj : std::as_const(images)) {
        if (!why.isEmpty()) break;
        bool changed = false;
        if (!blackOut(page, obj, userArea, &changed)) why = QStringLiteral("unreadable image");
        inPlace |= changed;
    }

    if (!why.isEmpty()) {
        for (const PathCut &cut : paths)
            for (FPDF_PAGEOBJECT part : cut.parts) FPDFPageObj_Destroy(part);
        if (textPage) FPDFText_ClosePage(textPage);
        qInfo() << "[Redaction] page" << pageIndex + 1 << "is redacted as an image:" << why;
        FPDFPage_SetRotation(page, turns);
        return rasterise(page, shownAreas, shown, raster);
    }

    std::vector<std::shared_ptr<PdfiumText::Typeset>> rest;
    for (const TextLayout::OriginalLine &line : std::as_const(text.rest))
        rest.push_back(PdfiumTextCut::setAgain(doc, page, textPage, pageIndex, line, nullptr));
    FPDFText_ClosePage(textPage);

    std::vector<FPDF_PAGEOBJECT> removed;
    const auto take = [&](FPDF_PAGEOBJECT obj) {
        if (!FPDFPage_RemoveObject(page, obj)) return false;
        removed.push_back(obj);
        return true;
    };
    bool ok = true;
    for (FPDF_PAGEOBJECT obj : std::as_const(text.cut)) ok = ok && take(obj);
    for (FPDF_PAGEOBJECT obj : std::as_const(dropped)) ok = ok && take(obj);
    for (const PathCut &cut : paths) {
        const int index = indexOf(page, cut.obj);
        for (int k = 0; k < cut.parts.size(); ++k)
            if (!FPDFPage_InsertObjectAtIndex(page, cut.parts.at(k), size_t(index + k)))
                FPDFPageObj_Destroy(cut.parts.at(k));
        ok = ok && take(cut.obj);
    }
    dropAnnotations(page, areas, space);
    for (const auto &line : rest) line->insertInto(page, space);
    cover(page, areas, space);
    for (FPDF_PAGEOBJECT obj : removed) FPDFPageObj_Destroy(obj);
    FPDFPage_SetRotation(page, turns);
    if (!ok) qWarning() << "[Redaction] an object on page" << pageIndex + 1 << "could not be removed";
    return ok;
}

QStringList PdfiumRedaction::leaks(FPDF_DOCUMENT doc, FPDF_PAGE page, const QList<QRectF> &areas)
{
    Q_UNUSED(doc)
    QStringList found;
    const PdfiumPageSpace space(page);
    QList<QRectF> inner;
    for (const QRectF &area : areas)
        if (area.width() > 1.0 && area.height() > 1.0) inner.append(area.adjusted(0.5, 0.5, -0.5, -0.5));
    if (inner.isEmpty()) return found;

    if (FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page)) {
        for (int i = 0, n = FPDFText_CountChars(textPage); i < n; ++i) {
            double l = 0, r = 0, b = 0, t = 0;
            if (FPDFText_IsGenerated(textPage, i) == 1 || !FPDFText_GetCharBox(textPage, i, &l, &r, &b, &t))
                continue;
            if (touches(inner, space.toView(l, b, r, t))) {
                found << QStringLiteral("text");
                break;
            }
        }
        FPDFText_ClosePage(textPage);
    } else {
        found << QStringLiteral("unreadable text");
    }

    const QPainterPath userInner = areaPath(inner, space, 1.0);
    const QPainterPath fineInner = areaPath(inner, space, kFine);
    for (int i = 0, n = FPDFPage_CountObjects(page); i < n; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        const QRectF box = viewBounds(obj, space);
        if (!touches(inner, grown(box))) continue;
        switch (FPDFPageObj_GetType(obj)) {
        case FPDF_PAGEOBJ_TEXT:
            if (covered(areas, box)) found << QStringLiteral("text object");
            break;
        case FPDF_PAGEOBJ_IMAGE:
            if (!imageBlackUnder(obj, userInner)) found << QStringLiteral("image");
            break;
        case FPDF_PAGEOBJ_PATH: {
            const std::optional<Ink> ink = inkOf(obj);
            if (!ink) { found << QStringLiteral("unreadable path"); break; }
            const bool bar = ink->stroke.isEmpty() && ink->fillColor == QColor(Qt::black)
                          && covered(areas, box.adjusted(0.5, 0.5, -0.5, -0.5));
            if (!bar && (ink->fill.intersects(fineInner) || ink->stroke.intersects(fineInner)))
                found << QStringLiteral("path");
            break;
        }
        default:
            found << QStringLiteral("form or shading");
            break;
        }
    }

    const QRectF shown = shownPage(page, space);
    const double scale = qMin(2.0, std::sqrt(8.0e6 / qMax(1.0, shown.width() * shown.height())));
    if (FPDF_BITMAP bitmap = shown.isEmpty() ? nullptr : renderShown(page, shown, scale)) {
        const auto *bits = static_cast<const uchar *>(FPDFBitmap_GetBuffer(bitmap));
        const int stride = FPDFBitmap_GetStride(bitmap);
        bool black = true;
        for (const QRectF &area : inner) {
            const QRect px = pixelsOf(area, shown, bitmap).adjusted(1, 1, -1, -1);
            for (int y = px.top(); black && y <= px.bottom(); ++y)
                for (int x = px.left(); black && x <= px.right(); ++x) {
                    const uchar *p = bits + qsizetype(y) * stride + qsizetype(x) * 4;
                    black = p[0] <= 2 && p[1] <= 2 && p[2] <= 2;
                }
        }
        FPDFBitmap_Destroy(bitmap);
        if (!black) found << QStringLiteral("uncovered");
    } else {
        found << QStringLiteral("unrendered");
    }
    return found;
}

#endif
