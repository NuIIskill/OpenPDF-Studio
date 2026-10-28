#include "engine/document/PdfiumEdits.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumFonts.hpp"
#include "engine/document/PdfiumImages.hpp"
#include "engine/document/PdfiumPageSpace.hpp"
#include "engine/document/PdfiumText.hpp"
#include "engine/document/PdfiumTextRules.hpp"
#include "engine/edit/EditSession.hpp"
#include "engine/edit/TextLayout.hpp"

#include "fpdf_annot.h"
#include "fpdf_doc.h"
#include "fpdf_edit.h"
#include "fpdf_text.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace {

QRectF objectBoundsQt(FPDF_PAGEOBJECT obj, const PdfiumPageSpace &space)
{
    float left = 0, bottom = 0, right = 0, top = 0;
    if (!FPDFPageObj_GetBounds(obj, &left, &bottom, &right, &top)) return {};
    return space.toView(left, bottom, right, top);
}

FS_RECTF pdfRect(const QRectF &view, const PdfiumPageSpace &space)
{
    const QRectF r = space.toPdf(view);
    FS_RECTF rect {};
    rect.left   = static_cast<float>(r.left());
    rect.right  = static_cast<float>(r.right());
    rect.bottom = static_cast<float>(r.top());
    rect.top    = static_cast<float>(r.bottom());
    return rect;
}

QList<QRectF> eraseAreas(const EditSession::Edit &edit)
{
    if (!edit.eraseRects.isEmpty()) return edit.eraseRects;
    return { edit.pdfBounds };
}

QList<FPDF_PAGEOBJECT> textObjectsIn(FPDF_PAGE page, const QList<QRectF> &areas,
                                     const PdfiumPageSpace &space)
{
    QList<FPDF_PAGEOBJECT> out;
    const int count = FPDFPage_CountObjects(page);
    for (int i = 0; i < count; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        if (!obj || FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_TEXT) continue;
        const QRectF bounds = objectBoundsQt(obj, space);
        if (bounds.isEmpty()) continue;
        for (const QRectF &area : areas)
            if (area.contains(bounds.center())) { out.append(obj); break; }
    }
    return out;
}

// The characters of text objects only partly inside the erased areas that lie
// outside them, each run as a line of its own, so they can be set again where
// they were: a frame may take part of an object.
QList<TextLayout::OriginalLine> cutRemainders(FPDF_TEXTPAGE textPage, const QList<QRectF> &areas,
                                              const PdfiumPageSpace &space,
                                              QList<FPDF_PAGEOBJECT> *cut)
{
    struct Char { QString text; QRectF box; QPointF origin; bool inside; bool ink; };
    QHash<FPDF_PAGEOBJECT, QList<Char>> byObject;
    QList<FPDF_PAGEOBJECT> order;
    QSet<FPDF_PAGEOBJECT> turned;
    for (int i = 0, n = FPDFText_CountChars(textPage); i < n; ++i) {
        FPDF_PAGEOBJECT obj = FPDFText_GetTextObject(textPage, i);
        double l = 0, r = 0, b = 0, t = 0, x = 0, y = 0;
        if (!obj || FPDFText_IsGenerated(textPage, i) == 1
                || !FPDFText_GetCharBox(textPage, i, &l, &r, &b, &t)
                || !FPDFText_GetCharOrigin(textPage, i, &x, &y))
            continue;
        const uint code = FPDFText_GetUnicode(textPage, i);
        if (code == 0) continue;
        // Turned text would be set upright; it is only ever taken out whole.
        if (!space.upright(FPDFText_GetCharAngle(textPage, i))) turned.insert(obj);
        const QRectF box = space.toView(l, b, r, t);
        bool inside = false;
        for (const QRectF &area : areas) inside |= area.contains(box.center());
        if (!byObject.contains(obj)) order.append(obj);
        const QString text = QString::fromUcs4(reinterpret_cast<const char32_t *>(&code), 1);
        QPointF origin = space.toView(x, y);
        // The second letter of a ligature starts where the first one's advance ends.
        if (const QList<Char> &before = byObject[obj]; !before.isEmpty()
                && std::abs(before.last().origin.x() - origin.x()) < 1e-3 && before.last().box == box) {
            const uint first = before.last().text.toUcs4().value(0);
            origin.rx() += PdfiumFonts::glyphWidth(FPDFTextObj_GetFont(obj), first,
                                                   PdfiumTextRules::effectiveFontSize(textPage, i));
        }
        byObject[obj].append({ text, box, origin, inside,
                               !text.trimmed().isEmpty() && !box.isEmpty() });
    }

    QList<TextLayout::OriginalLine> out;
    for (FPDF_PAGEOBJECT obj : std::as_const(order)) {
        const QList<Char> &chars = byObject[obj];
        // A space inside is taken too, but only ink decides whether all is:
        // a line's area ends before the spaces that end it.
        const bool any = std::any_of(chars.cbegin(), chars.cend(),
                                     [](const Char &c) { return c.inside; });
        const bool all = std::all_of(chars.cbegin(), chars.cend(),
                                     [](const Char &c) { return !c.ink || c.inside; });
        if (!any || all || turned.contains(obj)) continue;
        cut->append(obj);
        TextLayout::OriginalLine run;
        const auto flush = [&]() {
            if (!run.text.trimmed().isEmpty()) out.append(run);
            run = {};
        };
        for (const Char &c : chars) {
            if (c.inside) { flush(); continue; }
            if (run.text.isEmpty()) {
                run.baseline = c.origin.y();
                run.rect     = c.box;
            }
            run.rect = run.rect.united(c.box);
            for (qsizetype k = 0; k < c.text.size(); ++k) {
                run.charX.append(c.origin.x());
                run.objectStart.append(run.text.isEmpty() && k == 0);
            }
            run.text += c.text;
            run.endX  = c.box.right();
        }
        flush();
    }
    return out;
}

struct Blank {
    int                    edit { -1 };
    QRectF                 area;
    QList<FPDF_PAGEOBJECT> objects;
    PdfiumText::Origin     origin;
};

// The blank whose objects a text edit replaces: the one at its source, or
// the one it overlaps most.
const Blank *blankOf(const QList<Blank> &blanks, const EditSession::Edit &edit)
{
    if (edit.sourceRect.isNull() || (edit.sourcePage >= 0 && edit.sourcePage != edit.page))
        return nullptr;
    const Blank *best = nullptr;
    double bestOverlap = 0.0;
    for (const Blank &b : blanks) {
        if (!b.origin.font) continue;
        if (b.area == edit.sourceRect) return &b;
        const QRectF hit = b.area.intersected(edit.sourceRect);
        const double area = hit.width() * hit.height();
        if (area > bestOverlap) { bestOverlap = area; best = &b; }
    }
    return best;
}

// Paints over the scanned pixels of text that is replaced, in the paper's colour.
void coverScan(FPDF_PAGE page, const QList<QRectF> &areas, const QColor &paper,
               const PdfiumPageSpace &space)
{
    const QColor fill = paper.isValid() ? paper : QColor(Qt::white);
    for (const QRectF &area : areas) {
        const QRectF r = space.toPdf(area);
        FPDF_PAGEOBJECT rect = FPDFPageObj_CreateNewRect(float(r.left()), float(r.top()),
                                                         float(r.width()), float(r.height()));
        if (!rect) continue;
        FPDFPageObj_SetFillColor(rect, fill.red(), fill.green(), fill.blue(), 255);
        FPDFPath_SetDrawMode(rect, FPDF_FILLMODE_WINDING, false);
        FPDFPage_InsertObject(page, rect);
    }
}

void insertImage(FPDF_DOCUMENT doc, FPDF_PAGE page,
                 const EditSession::ImageEdit &edit, const PdfiumPageSpace &space)
{
    if (edit.image.isNull()) return;

    const QImage src = edit.image.convertToFormat(QImage::Format_ARGB32);
    FPDF_BITMAP bitmap = FPDFBitmap_Create(src.width(), src.height(), 1);
    if (!bitmap) return;

    auto *dst = static_cast<unsigned char *>(FPDFBitmap_GetBuffer(bitmap));
    const int stride = FPDFBitmap_GetStride(bitmap);
    for (int y = 0; y < src.height(); ++y)
        memcpy(dst + static_cast<size_t>(y) * stride, src.constScanLine(y),
               static_cast<size_t>(src.bytesPerLine()));

    FPDF_PAGEOBJECT obj = FPDFPageObj_NewImageObj(doc);
    if (!obj) { FPDFBitmap_Destroy(bitmap); return; }
    FPDFImageObj_SetBitmap(&page, 1, obj, bitmap);

    const QRectF &b = edit.pdfBounds;
    const QPointF across = space.rightward() * b.width(), up = space.upward() * b.height();
    const QPointF origin = space.toPdf(b.bottomLeft());
    FPDFImageObj_SetMatrix(obj, across.x(), across.y(), up.x(), up.y(), origin.x(), origin.y());
    FPDFPage_InsertObject(page, obj);
    FPDFBitmap_Destroy(bitmap);
}

void insertDrawStroke(FPDF_PAGE page, const EditSession::DrawStroke &stroke,
                      const PdfiumPageSpace &space)
{
    if (stroke.points.isEmpty()) return;

    const QPointF first = space.toPdf(stroke.points.first());
    FPDF_PAGEOBJECT path = FPDFPageObj_CreateNewPath(static_cast<float>(first.x()),
                                                     static_cast<float>(first.y()));
    if (!path) return;

    if (stroke.points.size() == 1) {
        const QPointF dot = space.toPdf(stroke.points.first() + QPointF(0.01, 0.0));
        FPDFPath_LineTo(path, static_cast<float>(dot.x()), static_cast<float>(dot.y()));
    } else {
        for (int i = 1; i < stroke.points.size(); ++i) {
            const QPointF point = space.toPdf(stroke.points.at(i));
            FPDFPath_LineTo(path, static_cast<float>(point.x()), static_cast<float>(point.y()));
        }
    }

    const QColor color = stroke.color;
    FPDFPageObj_SetStrokeColor(path, color.red(), color.green(), color.blue(),
                               color.alpha());
    FPDFPageObj_SetStrokeWidth(path, static_cast<float>(stroke.widthPt));
    FPDFPageObj_SetLineCap(path, FPDF_LINECAP_ROUND);
    FPDFPageObj_SetLineJoin(path, FPDF_LINEJOIN_ROUND);
    FPDFPath_SetDrawMode(path, FPDF_FILLMODE_NONE, true);
    FPDFPage_InsertObject(page, path);
}

QString uriOf(FPDF_DOCUMENT doc, FPDF_ANNOTATION annot)
{
    const FPDF_LINK link = FPDFAnnot_GetLink(annot);
    const FPDF_ACTION action = link ? FPDFLink_GetAction(link) : nullptr;
    if (!action || FPDFAction_GetType(action) != PDFACTION_URI) return {};
    const unsigned long bytes = FPDFAction_GetURIPath(doc, action, nullptr, 0);
    if (bytes <= 1) return {};
    std::vector<char> buffer(bytes, 0);
    if (FPDFAction_GetURIPath(doc, action, buffer.data(), bytes) <= 1) return {};
    return QString::fromUtf8(buffer.data());
}

QRectF annotationBounds(FPDF_ANNOTATION annot, const PdfiumPageSpace &space)
{
    FS_RECTF rect {};
    if (!FPDFAnnot_GetRect(annot, &rect)) return {};
    return space.toView(rect.left, rect.bottom, rect.right, rect.top);
}

bool sameBounds(const QRectF &a, const QRectF &b)
{
    constexpr double tolerance = 0.5;
    return std::abs(a.left() - b.left()) <= tolerance
        && std::abs(a.top() - b.top()) <= tolerance
        && std::abs(a.right() - b.right()) <= tolerance
        && std::abs(a.bottom() - b.bottom()) <= tolerance;
}

void setAnnotationRect(FPDF_ANNOTATION annot, const QRectF &bounds, const PdfiumPageSpace &space)
{
    const FS_RECTF rect = pdfRect(bounds, space);
    FPDFAnnot_SetRect(annot, &rect);
}

void appendLinkQuadPoints(FPDF_ANNOTATION annot, const QList<QRectF> &rects,
                          const PdfiumPageSpace &space)
{
    for (const QRectF &rect : rects) {
        const QPointF p1 = space.toPdf(rect.topLeft()), p2 = space.toPdf(rect.topRight());
        const QPointF p3 = space.toPdf(rect.bottomLeft()), p4 = space.toPdf(rect.bottomRight());
        FS_QUADPOINTSF quad {};
        quad.x1 = static_cast<float>(p1.x()); quad.y1 = static_cast<float>(p1.y());
        quad.x2 = static_cast<float>(p2.x()); quad.y2 = static_cast<float>(p2.y());
        quad.x3 = static_cast<float>(p3.x()); quad.y3 = static_cast<float>(p3.y());
        quad.x4 = static_cast<float>(p4.x()); quad.y4 = static_cast<float>(p4.y());
        FPDFAnnot_AppendAttachmentPoints(annot, &quad);
    }
}

QString pageObjectMarkName(FPDF_PAGEOBJECTMARK mark)
{
    unsigned long bytes = 0;
    if (!mark || !FPDFPageObjMark_GetName(mark, nullptr, 0, &bytes) || bytes <= 2)
        return {};
    std::vector<unsigned short> buffer(bytes / 2 + 1, 0);
    if (!FPDFPageObjMark_GetName(mark, buffer.data(), bytes, &bytes)) return {};
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.data()));
}

FPDF_PAGEOBJECTMARK linkStyleMark(FPDF_PAGEOBJECT object)
{
    const int count = FPDFPageObj_CountMarks(object);
    for (int i = 0; i < count; ++i)
        if (pageObjectMarkName(
                FPDFPageObj_GetMark(object, static_cast<unsigned long>(i)))
                == QLatin1String("OpenPDFLinkStyle"))
            return FPDFPageObj_GetMark(object, static_cast<unsigned long>(i));
    return nullptr;
}

FPDF_PAGEOBJECTMARK markLinkStyle(FPDF_DOCUMENT doc, FPDF_PAGEOBJECT object,
                                  bool rememberFill)
{
    if (!object) return nullptr;
    if (FPDF_PAGEOBJECTMARK existing = linkStyleMark(object)) return existing;
    FPDF_PAGEOBJECTMARK mark = FPDFPageObj_AddMark(object, "OpenPDFLinkStyle");
    if (!mark || !rememberFill) return mark;

    unsigned int r = 0, g = 0, b = 0, a = 255;
    FPDFPageObj_GetFillColor(object, &r, &g, &b, &a);
    FPDFPageObjMark_SetIntParam(doc, object, mark, "R", static_cast<int>(r));
    FPDFPageObjMark_SetIntParam(doc, object, mark, "G", static_cast<int>(g));
    FPDFPageObjMark_SetIntParam(doc, object, mark, "B", static_cast<int>(b));
    FPDFPageObjMark_SetIntParam(doc, object, mark, "A", static_cast<int>(a));
    return mark;
}

FPDF_PAGEOBJECT blueUnderline(FPDF_DOCUMENT doc, const QRectF &rect,
                              const PdfiumPageSpace &space)
{
    const QPointF from = space.toPdf(QPointF(rect.left(), rect.bottom() - 0.8));
    const QPointF to   = space.toPdf(QPointF(rect.right(), rect.bottom() - 0.8));
    FPDF_PAGEOBJECT path = FPDFPageObj_CreateNewPath(static_cast<float>(from.x()),
                                                     static_cast<float>(from.y()));
    if (!path) return nullptr;
    FPDFPath_LineTo(path, static_cast<float>(to.x()), static_cast<float>(to.y()));
    FPDFPageObj_SetStrokeColor(path, 0, 102, 204, 255);
    FPDFPageObj_SetStrokeWidth(path, 0.7f);
    FPDFPath_SetDrawMode(path, FPDF_FILLMODE_NONE, true);
    markLinkStyle(doc, path, false);
    return path;
}

bool centerIn(const QRectF &box, const QList<QRectF> &rects)
{
    for (const QRectF &rect : rects)
        if (rect.contains(box.center())) return true;
    return false;
}

void colorLinkedText(FPDF_DOCUMENT doc, FPDF_PAGE page, int pageIndex,
                     const EditSession &session, const PdfiumPageSpace &space)
{
    QList<QRectF> rects;
    for (const EditSession::LinkEdit &edit : session.linkEdits()) {
        if (edit.page != pageIndex || edit.existing || edit.removed || !edit.colorText)
            continue;
        rects += edit.textRects.isEmpty() ? QList<QRectF>{ edit.pdfBounds }
                                         : edit.textRects;
    }
    if (rects.isEmpty()) return;

    std::vector<FPDF_PAGEOBJECT> objects;

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    if (!textPage) return;
    const int count = FPDFText_CountChars(textPage);
    objects.reserve(count > 0 ? static_cast<size_t>(count) : 0);
    for (int i = 0; i < count; ++i) {
        double left = 0, right = 0, bottom = 0, top = 0;
        if (!FPDFText_GetCharBox(textPage, i, &left, &right, &bottom, &top)) continue;
        const QRectF box = space.toView(left, bottom, right, top);
        if (!centerIn(box, rects)) continue;

        FPDF_PAGEOBJECT object = FPDFText_GetTextObject(textPage, i);
        if (object && std::find(objects.begin(), objects.end(), object) == objects.end())
            objects.push_back(object);
    }
    FPDFText_ClosePage(textPage);

    for (FPDF_PAGEOBJECT object : objects) {
        markLinkStyle(doc, object, true);
        FPDFPageObj_SetFillColor(object, 0, 102, 204, 255);
    }
    for (const QRectF &rect : rects)
        if (FPDF_PAGEOBJECT underline = blueUnderline(doc, rect, space))
            FPDFPage_InsertObject(page, underline);
}

void removeLinkStyle(FPDF_DOCUMENT doc, FPDF_PAGE page, const QList<QRectF> &rects,
                     const PdfiumPageSpace &space)
{
    Q_UNUSED(doc)
    std::vector<FPDF_PAGEOBJECT> removed;
    const int count = FPDFPage_CountObjects(page);
    for (int i = 0; i < count; ++i) {
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, i);
        FPDF_PAGEOBJECTMARK mark = object ? linkStyleMark(object) : nullptr;
        if (!mark) continue;
        const QRectF bounds = objectBoundsQt(object, space);
        if (!rects.isEmpty() && !centerIn(bounds, rects)) {
            bool overlaps = false;
            for (const QRectF &rect : rects)
                if (rect.intersects(bounds)) { overlaps = true; break; }
            if (!overlaps) continue;
        }
        if (FPDFPageObj_GetType(object) == FPDF_PAGEOBJ_TEXT) {
            int r = 0, g = 0, b = 0, a = 255;
            FPDFPageObjMark_GetParamIntValue(mark, "R", &r);
            FPDFPageObjMark_GetParamIntValue(mark, "G", &g);
            FPDFPageObjMark_GetParamIntValue(mark, "B", &b);
            FPDFPageObjMark_GetParamIntValue(mark, "A", &a);
            FPDFPageObj_SetFillColor(object, r, g, b, a);
            FPDFPageObj_RemoveMark(object, mark);
        } else if (FPDFPage_RemoveObject(page, object)) {
            removed.push_back(object);
        }
    }
    for (FPDF_PAGEOBJECT object : removed) FPDFPageObj_Destroy(object);
}

void applyLinkEdits(FPDF_DOCUMENT doc, FPDF_PAGE page, int pageIndex,
                    const EditSession &session, const PdfiumPageSpace &space)
{
    for (const EditSession::LinkEdit &edit : session.linkEdits()) {
        if (edit.page != pageIndex || !edit.existing) continue;

        if (edit.removed && edit.colorText)
            removeLinkStyle(doc, page,
                edit.textRects.isEmpty() ? QList<QRectF>{ edit.pdfBounds }
                                         : edit.textRects,
                space);

        const int count = FPDFPage_GetAnnotCount(page);
        for (int i = 0; i < count; ++i) {
            FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
            if (!annot) continue;
            const bool match = FPDFAnnot_GetSubtype(annot) == FPDF_ANNOT_LINK
                && uriOf(doc, annot) == edit.originalUrl
                && sameBounds(annotationBounds(annot, space), edit.originalBounds);
            if (!match) {
                FPDFPage_CloseAnnot(annot);
                continue;
            }
            if (edit.removed) {
                FPDFPage_CloseAnnot(annot);
                FPDFPage_RemoveAnnot(page, i);
            } else {
                setAnnotationRect(annot, edit.pdfBounds, space);
                FPDFAnnot_SetURI(annot, edit.url.toUtf8().constData());
                FPDFPage_CloseAnnot(annot);
            }
            break;
        }
    }

    for (const EditSession::LinkEdit &edit : session.linkEdits()) {
        if (edit.page != pageIndex || edit.existing || edit.removed) continue;
        FPDF_ANNOTATION annot = FPDFPage_CreateAnnot(page, FPDF_ANNOT_LINK);
        if (!annot) continue;
        setAnnotationRect(annot, edit.pdfBounds, space);
        appendLinkQuadPoints(annot, edit.textRects, space);
        FPDFAnnot_SetBorder(annot, 0.0f, 0.0f, 0.0f);
        FPDFAnnot_SetURI(annot, edit.url.toUtf8().constData());
        FPDFPage_CloseAnnot(annot);
    }
}

}

void PdfiumEdits::applyToPage(FPDF_DOCUMENT doc, FPDF_PAGE page, int pageIndex,
                              const EditSession &session, const QHash<int, FPDF_PAGE> *sourcePages)
{
    if (!doc || !page) return;
    const PdfiumPageSpace space(page);
    const QList<EditSession::Edit> &edits = session.edits();
    const auto onPage = [pageIndex](const EditSession::Edit &e) {
        return e.page == pageIndex && e.formField.isEmpty();
    };

    // Images are found by their place among the original objects, so they go first.
    std::vector<FPDF_PAGEOBJECT> removed;
    QList<EditSession::ImageChange> imageChanges;
    for (const EditSession::ImageChange &c : session.imageChanges())
        if (c.page == pageIndex) imageChanges.append(c);
    if (!imageChanges.isEmpty()) PdfiumImages::apply(doc, page, imageChanges, removed);

    // Everything is read while the original objects are all still there.
    FPDF_TEXTPAGE textPage = nullptr;
    for (const EditSession::Edit &e : edits)
        if (onPage(e) && !textPage) textPage = FPDFText_LoadPage(page);
    QList<Blank> blanks;
    for (int i = 0; i < edits.size(); ++i) {
        const EditSession::Edit &e = edits.at(i);
        if (!onPage(e) || !e.newText.isNull()) continue;
        Blank b;
        b.edit    = i;
        b.area    = e.pdfBounds;
        b.objects = textObjectsIn(page, eraseAreas(e), space);
        // A scan's text layer is invisible; what is written there takes nothing of it.
        if (!e.scanned) b.origin = PdfiumText::originOf(b.objects, textPage);
        blanks.append(b);
    }
    QHash<int, std::shared_ptr<PdfiumText::Typeset>> plans;
    for (int i = 0; i < edits.size(); ++i) {
        const EditSession::Edit &e = edits.at(i);
        if (!onPage(e) || e.newText.isEmpty()) continue;
        const Blank *blank = blankOf(blanks, e);
        plans.insert(i, std::make_shared<PdfiumText::Typeset>(
                            doc, page, textPage, e, blank ? &blank->origin : nullptr, true,
                            sourcePages));
    }
    // What of a cut object lies outside every erased area is set again in
    // place, in the object's own style, by a plan that keeps no object.
    QList<FPDF_PAGEOBJECT> cut;
    std::vector<std::shared_ptr<PdfiumText::Typeset>> remainders;
    if (textPage && !blanks.isEmpty()) {
        QList<QRectF> areas;
        for (const Blank &b : std::as_const(blanks)) areas += eraseAreas(edits.at(b.edit));
        for (const TextLayout::OriginalLine &line : cutRemainders(textPage, areas, space, &cut)) {
            EditSession::Edit rest;
            rest.page          = pageIndex;
            rest.sourcePage    = pageIndex;
            // Room to spare, so the run is never wrapped instead of set in place.
            rest.pdfBounds     = QRectF(line.rect.left(), line.rect.top(),
                                        line.rect.width() * 2.0 + 100.0, line.rect.height());
            rest.newText       = line.text;
            rest.originalLines = { line };
            rest.hasTextOrigin = true;
            rest.textOriginOffset = QPointF(line.charX.first(), line.baseline)
                                  - rest.pdfBounds.topLeft();
            remainders.push_back(std::make_shared<PdfiumText::Typeset>(
                doc, page, textPage, rest, nullptr, true, sourcePages, false));
        }
    }
    if (textPage) FPDFText_ClosePage(textPage);

    for (const Blank &b : blanks) {
        QList<FPDF_PAGEOBJECT> doomed = b.objects;
        for (auto it = plans.cbegin(); it != plans.cend(); ++it) {
            const EditSession::Edit &t = edits.at(it.key());
            if (!t.originalLines.isEmpty() && t.sourceRect == b.area && it.value()->anchored())
                doomed = it.value()->replaces(b.objects, space);
        }
        for (FPDF_PAGEOBJECT obj : doomed)
            if (FPDFPage_RemoveObject(page, obj)) removed.push_back(obj);
    }
    for (FPDF_PAGEOBJECT obj : std::as_const(cut))
        if (std::find(removed.cbegin(), removed.cend(), obj) == removed.cend()
                && FPDFPage_RemoveObject(page, obj))
            removed.push_back(obj);
    for (const Blank &b : std::as_const(blanks)) {
        const EditSession::Edit &e = edits.at(b.edit);
        if (e.scanned) coverScan(page, eraseAreas(e), e.bgColor, space);
    }
    for (const auto &rest : remainders) rest->insertInto(page, space);
    for (int i = 0; i < edits.size(); ++i)
        if (const auto plan = plans.value(i)) plan->insertInto(page, space);

    for (const EditSession::ImageEdit &ie : session.imageEdits())
        if (ie.page == pageIndex) insertImage(doc, page, ie, space);

    for (const EditSession::DrawStroke &stroke : session.drawStrokes())
        if (stroke.page == pageIndex) insertDrawStroke(page, stroke, space);

    colorLinkedText(doc, page, pageIndex, session, space);
    applyLinkEdits(doc, page, pageIndex, session, space);

    for (FPDF_PAGEOBJECT obj : removed) FPDFPageObj_Destroy(obj);
}

namespace {

void setAnnotationString(FPDF_ANNOTATION annotation, const char *key,
                         const QString &value)
{
    const std::u16string utf16 = value.toStdU16String();
    FPDFAnnot_SetStringValue(annotation, key,
        reinterpret_cast<FPDF_WIDESTRING>(utf16.c_str()));
}

QString noteString(FPDF_ANNOTATION annotation, const char *key)
{
    const unsigned long bytes = FPDFAnnot_GetStringValue(annotation, key, nullptr, 0);
    if (bytes <= sizeof(char16_t)) return {};
    std::vector<char16_t> buffer((bytes + sizeof(char16_t) - 1) / sizeof(char16_t));
    const unsigned long written = FPDFAnnot_GetStringValue(
        annotation, key, reinterpret_cast<FPDF_WCHAR *>(buffer.data()), bytes);
    if (written <= sizeof(char16_t)) return {};
    return QString::fromUtf16(buffer.data(),
        static_cast<qsizetype>(written / sizeof(char16_t) - 1));
}

bool sameNoteBounds(const QRectF &a, const QRectF &b)
{
    return qAbs(a.left() - b.left()) < 0.5
        && qAbs(a.top() - b.top()) < 0.5
        && qAbs(a.width() - b.width()) < 0.5
        && qAbs(a.height() - b.height()) < 0.5;
}

}

namespace {

// The style an edit's text falls back to: that of the text it replaces.
PdfiumText::Origin originFor(FPDF_PAGE page, FPDF_TEXTPAGE textPage, const EditSession::Edit &edit)
{
    PdfiumText::Origin origin;
    if (!edit.scanned && !edit.sourceRect.isNull()
            && (edit.sourcePage < 0 || edit.sourcePage == edit.page)) {
        const QList<QRectF> areas = edit.eraseRects.isEmpty() ? QList<QRectF>{ edit.sourceRect }
                                                              : edit.eraseRects;
        origin = PdfiumText::originOf(textObjectsIn(page, areas, PdfiumPageSpace(page)),
                                      textPage);
    }
    return origin;
}

}

TextLayout::Metrics PdfiumEdits::metrics(FPDF_DOCUMENT doc, FPDF_PAGE page,
                                         FPDF_TEXTPAGE textPage, const EditSession::Edit &edit)
{
    if (!doc || !page) return {};
    const PdfiumText::Origin origin = originFor(page, textPage, edit);
    const PdfiumText::Typeset typeset(doc, page, textPage, edit,
                                      origin.font ? &origin : nullptr, false);
    return typeset.metrics();
}

QList<TextLayout::OriginalLine> PdfiumEdits::laidLines(FPDF_DOCUMENT doc, FPDF_PAGE page,
                                                       FPDF_TEXTPAGE textPage,
                                                       const EditSession::Edit &edit,
                                                       QList<QList<TextLayout::Source>> *sources,
                                                       QList<QList<TextLayout::Mark>> *marks)
{
    if (!doc || !page || edit.newText.isEmpty()) return {};
    const PdfiumText::Origin origin = originFor(page, textPage, edit);
    const PdfiumText::Typeset typeset(doc, page, textPage, edit,
                                      origin.font ? &origin : nullptr, true);
    return typeset.laidLines(sources, marks);
}

void PdfiumEdits::textAlone(FPDF_DOCUMENT doc, FPDF_PAGE page, const QList<EditSession::Edit> &edits)
{
    // The page then holds the edits' text and nothing else: every line set
    // where it goes, also those that would stay the original objects.
    if (!doc || !page) return;
    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    std::vector<std::unique_ptr<PdfiumText::Typeset>> plans;
    for (const EditSession::Edit &e : edits) {
        if (e.newText.isEmpty() || !e.formField.isEmpty()) continue;
        const PdfiumText::Origin origin = originFor(page, textPage, e);
        plans.push_back(std::make_unique<PdfiumText::Typeset>(
            doc, page, textPage, e, origin.font ? &origin : nullptr, true, nullptr, false));
    }
    // From the front: PDFium looks each object up from there, so this stays linear.
    while (FPDFPage_CountObjects(page) > 0) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, 0);
        if (!FPDFPage_RemoveObject(page, obj)) break;
        FPDFPageObj_Destroy(obj);
    }
    const PdfiumPageSpace space(page);
    for (const auto &plan : plans) plan->insertInto(page, space);
    plans.clear();
    if (textPage) FPDFText_ClosePage(textPage);
}

void PdfiumEdits::applyNoteEdits(FPDF_PAGE page, int pageIndex,
                                 const EditSession &session)
{
    if (!page) return;
    const PdfiumPageSpace space(page);

    for (const EditSession::NoteEdit &edit : session.noteEdits()) {
        if (edit.page != pageIndex || !edit.existing) continue;

        const int count = FPDFPage_GetAnnotCount(page);
        for (int i = 0; i < count; ++i) {
            FPDF_ANNOTATION annotation = FPDFPage_GetAnnot(page, i);
            if (!annotation) continue;
            const bool rightType = FPDFAnnot_GetSubtype(annotation) == FPDF_ANNOT_TEXT;
            const QString id = rightType ? noteString(annotation, "NM") : QString();
            const bool matches = rightType && (
                (!edit.originalId.isEmpty() && id == edit.originalId)
                || (edit.originalId.isEmpty()
                    && noteString(annotation, "Contents") == edit.originalText
                    && sameNoteBounds(annotationBounds(annotation, space),
                                      edit.originalBounds)));
            if (!matches) {
                FPDFPage_CloseAnnot(annotation);
                continue;
            }
            if (edit.removed) {
                FPDFPage_CloseAnnot(annotation);
                FPDFPage_RemoveAnnot(page, i);
            } else {
                setAnnotationRect(annotation, edit.pdfBounds, space);
                setAnnotationString(annotation, "NM", edit.id);
                setAnnotationString(annotation, "T", edit.title);
                setAnnotationString(annotation, "Contents", edit.text);
                setAnnotationString(annotation, "OpenPDFPinned",
                                    edit.pinned ? QStringLiteral("1")
                                                : QStringLiteral("0"));
                FPDFPage_CloseAnnot(annotation);
            }
            break;
        }
    }

    for (const EditSession::NoteEdit &edit : session.noteEdits()) {
        if (edit.page != pageIndex || edit.existing || edit.removed) continue;
        FPDF_ANNOTATION annotation = FPDFPage_CreateAnnot(page, FPDF_ANNOT_TEXT);
        if (!annotation) continue;
        setAnnotationRect(annotation, edit.pdfBounds, space);
        setAnnotationString(annotation, "NM", edit.id);
        setAnnotationString(annotation, "T", edit.title);
        setAnnotationString(annotation, "Contents", edit.text);
        setAnnotationString(annotation, "OpenPDFPinned",
                            edit.pinned ? QStringLiteral("1")
                                        : QStringLiteral("0"));
        FPDFAnnot_SetColor(annotation, FPDFANNOT_COLORTYPE_Color,
                           255, 204, 0, 255);
        FPDFPage_CloseAnnot(annotation);
    }
}

#endif
