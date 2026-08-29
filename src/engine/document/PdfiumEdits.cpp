#include "engine/document/PdfiumEdits.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumFonts.hpp"
#include "engine/edit/EditSession.hpp"
#include "engine/edit/TextWrap.hpp"

#include "fpdf_annot.h"
#include "fpdf_doc.h"
#include "fpdf_edit.h"
#include "fpdf_text.h"

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QtMath>

#include <cmath>
#include <vector>

namespace {

double toPdfY(double qtY, double pageHeight) { return pageHeight - qtY; }

QRectF objectBoundsQt(FPDF_PAGEOBJECT obj, double pageHeight)
{
    float left = 0, bottom = 0, right = 0, top = 0;
    if (!FPDFPageObj_GetBounds(obj, &left, &bottom, &right, &top)) return {};
    return QRectF(left, pageHeight - top, right - left, top - bottom);
}

QList<QRectF> eraseAreas(const EditSession::Edit &edit)
{
    if (!edit.eraseRects.isEmpty()) return edit.eraseRects;
    return { edit.pdfBounds };
}

bool removesOriginal(const EditSession::Edit &edit)
{
    return edit.newText.isNull();
}

struct Replaced {
    QRectF    area;
    QString   text;
    FPDF_FONT font   { nullptr };
    double    sizePt { 0.0 };
    std::vector<FPDF_PAGEOBJECT> removed;
};

double effectiveFontSize(FPDF_PAGEOBJECT obj)
{
    float size = 0.f;
    if (!FPDFTextObj_GetFontSize(obj, &size) || size <= 0.f) return 0.0;
    FS_MATRIX m {};
    if (!FPDFPageObj_GetMatrix(obj, &m)) return size;
    const double scale = std::hypot(m.c, m.d);
    return scale > 0.0 ? size * scale : size;
}

void removeTextIn(FPDF_PAGE page, const QList<QRectF> &areas, double pageHeight,
                  Replaced &out)
{
    const int count = FPDFPage_CountObjects(page);
    std::vector<FPDF_PAGEOBJECT> doomed;
    for (int i = 0; i < count; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        if (!obj || FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_TEXT) continue;

        const QRectF bounds = objectBoundsQt(obj, pageHeight);
        if (bounds.isEmpty()) continue;

        bool covered = false;
        for (const QRectF &area : areas) {
            if (area.contains(bounds.center())) { covered = true; break; }
        }
        if (!covered) continue;

        if (!out.font) {
            out.font   = FPDFTextObj_GetFont(obj);
            out.sizePt = effectiveFontSize(obj);
        }
        doomed.push_back(obj);
    }

    for (FPDF_PAGEOBJECT obj : doomed)
        if (FPDFPage_RemoveObject(page, obj))
            out.removed.push_back(obj);
}

const Replaced *originOf(const QList<Replaced> &all, const EditSession::Edit &edit)
{
    if (!removesOriginal(edit) && edit.sourceRect.isNull()) return nullptr;
    const QRectF key = edit.sourceRect.isNull() ? edit.pdfBounds : edit.sourceRect;

    const Replaced *best = nullptr;
    double bestOverlap = 0.0;
    for (const Replaced &r : all) {
        if (!r.font) continue;
        if (r.area == key) return &r;
        const QRectF hit = r.area.intersected(key);
        const double area = hit.width() * hit.height();
        if (area > bestOverlap) { bestOverlap = area; best = &r; }
    }
    return best;
}

bool fontCanRender(FPDF_FONT font, const QString &text, const QString &original)
{
    if (!font) return false;
    QSet<uint> known;
    for (const uint cp : original.toUcs4()) known.insert(cp);

    for (const uint cp : text.toUcs4()) {
        if (QChar::isSpace(cp) || known.contains(cp)) continue;
        float width = 0.f;
        if (!FPDFFont_GetGlyphWidth(font, cp, 12.f, &width) || width <= 0.f)
            return false;
        FPDF_GLYPHPATH path = FPDFFont_GetGlyphPath(font, cp, 12.f);
        if (!path || FPDFGlyphPath_CountGlyphSegments(path) <= 0)
            return false;
    }
    return true;
}

QByteArray standardFontLike(FPDF_FONT font, const EditSession::Edit &edit)
{
    QString family = edit.fontFamily;
    bool bold      = edit.bold;
    bool italic    = edit.italic;
    if (font && !edit.fontChanged) {
        char name[128] = {};
        if (FPDFFont_GetBaseFontName(font, name, sizeof(name)) > 0)
            family = QString::fromLatin1(name);
        constexpr int kSerifFlag = 1 << 1;
        if (FPDFFont_GetFlags(font) & kSerifFlag)
            family += QLatin1String(" Serif");
        bold   = bold   || FPDFFont_GetWeight(font) >= 600;
        int angle = 0;
        italic = italic || (FPDFFont_GetItalicAngle(font, &angle) && angle != 0);
    }
    return PdfiumFonts::standardFontFor(family, bold, italic);
}

double lineWidthPt(FPDF_FONT font, const QString &line, double size,
                   double charSpacing)
{
    double width = 0.0;
    int glyphs = 0;
    for (const uint cp : line.toUcs4()) {
        float advance = 0.f;
        if (FPDFFont_GetGlyphWidth(font, cp, static_cast<float>(size), &advance))
            width += advance;
        ++glyphs;
    }
    if (!qFuzzyIsNull(charSpacing) && glyphs > 1)
        width += charSpacing * (glyphs - 1);
    return width;
}

QPointF firstBaseline(const EditSession::Edit &edit, double fontSize,
                      double pageHeight, int lineCount)
{
    const bool customInset = edit.box.paddingPt > 0.0
                          || edit.box.verticalAlign != TextBoxProperties::VerticalAlign::Top;
    if (edit.hasTextOrigin && !customInset) {
        const QPointF qt = edit.pdfBounds.topLeft() + edit.textOriginOffset;
        return QPointF(qt.x(), toPdfY(qt.y(), pageHeight));
    }
    const int lines = qMax(1, lineCount);
    const double step = edit.lineSpacingPt > 0.0 ? edit.lineSpacingPt : fontSize * 1.2;
    const double contentHeight = fontSize + (lines - 1)
                               * (step + edit.box.paragraphSpacingPt);
    const double innerHeight = qMax(0.0, edit.pdfBounds.height() - 2 * edit.box.paddingPt);
    double offset = 0.0;
    if (edit.box.verticalAlign == TextBoxProperties::VerticalAlign::Center)
        offset = qMax(0.0, (innerHeight - contentHeight) / 2.0);
    else if (edit.box.verticalAlign == TextBoxProperties::VerticalAlign::Bottom)
        offset = qMax(0.0, innerHeight - contentHeight);
    const double baselineQt = edit.pdfBounds.top() + edit.box.paddingPt
                            + offset + fontSize * 0.8;
    return QPointF(edit.pdfBounds.left() + edit.box.paddingPt
                       + edit.box.indentLevel * 18.0,
                   toPdfY(baselineQt, pageHeight));
}

void rotateObject(FPDF_PAGEOBJECT object, const EditSession::Edit &edit,
                  double pageHeight)
{
    if (!object || qFuzzyIsNull(edit.box.rotationDeg)) return;
    const double a = qDegreesToRadians(-edit.box.rotationDeg);
    const double cs = std::cos(a), sn = std::sin(a);
    const QPointF center(edit.pdfBounds.center().x(),
                         toPdfY(edit.pdfBounds.center().y(), pageHeight));
    FPDFPageObj_Transform(object, cs, sn, -sn, cs,
                          center.x() - cs * center.x() + sn * center.y(),
                          center.y() - sn * center.x() - cs * center.y());
}

FPDF_PAGEOBJECT roundedRectPath(const QRectF &qtRect, double radius,
                                double pageHeight)
{
    const double l=qtRect.left(), r=qtRect.right();
    const double b=toPdfY(qtRect.bottom(),pageHeight), t=toPdfY(qtRect.top(),pageHeight);
    const double rad=qBound(0.0,radius,qMin(qtRect.width(),qtRect.height())/2.0);
    if (rad <= 0.0) return FPDFPageObj_CreateNewRect(l,b,qtRect.width(),qtRect.height());
    constexpr double k=.5522847498;
    FPDF_PAGEOBJECT p=FPDFPageObj_CreateNewPath(l+rad,b);
    FPDFPath_LineTo(p,r-rad,b); FPDFPath_BezierTo(p,r-rad+k*rad,b,r,b+rad-k*rad,r,b+rad);
    FPDFPath_LineTo(p,r,t-rad); FPDFPath_BezierTo(p,r,t-rad+k*rad,r-rad+k*rad,t,r-rad,t);
    FPDFPath_LineTo(p,l+rad,t); FPDFPath_BezierTo(p,l+rad-k*rad,t,l,t-rad+k*rad,l,t-rad);
    FPDFPath_LineTo(p,l,b+rad); FPDFPath_BezierTo(p,l,b+rad-k*rad,l+rad-k*rad,b,l+rad,b);
    FPDFPath_Close(p); return p;
}

void insertBoxDecoration(FPDF_PAGE page, const EditSession::Edit &edit,
                         double pageHeight)
{
    if (!edit.box.backgroundEnabled && !edit.box.borderEnabled) return;
    FPDF_PAGEOBJECT path=roundedRectPath(edit.pdfBounds,edit.box.cornerRadiusPt,pageHeight);
    if (!path) return;
    const int alpha=qRound(255*qBound(0.0,edit.box.opacity,1.0));
    if (edit.box.backgroundEnabled) {
        const QColor c=edit.box.backgroundColor;
        FPDFPageObj_SetFillColor(path,c.red(),c.green(),c.blue(),qRound(alpha*c.alphaF()));
    }
    const bool solidBorder = edit.box.borderEnabled
                          && edit.box.borderStyle == TextBoxProperties::BorderStyle::Solid;
    if (solidBorder) {
        const QColor c=edit.box.borderColor;
        FPDFPageObj_SetStrokeColor(path,c.red(),c.green(),c.blue(),qRound(alpha*c.alphaF()));
        FPDFPageObj_SetStrokeWidth(path,qMax(.1,edit.box.borderWidthPt));
    }
    FPDFPath_SetDrawMode(path,edit.box.backgroundEnabled?FPDF_FILLMODE_WINDING:FPDF_FILLMODE_NONE,
                         solidBorder);
    rotateObject(path,edit,pageHeight);
    FPDFPage_InsertObject(page,path);

    if (edit.box.borderEnabled && !solidBorder) {
        const QRectF r=edit.pdfBounds;
        const double left=r.left(),right=r.right(),bottom=toPdfY(r.bottom(),pageHeight),top=toPdfY(r.top(),pageHeight);
        FPDF_PAGEOBJECT dashed=FPDFPageObj_CreateNewPath(left,bottom);
        const double dash=edit.box.borderStyle==TextBoxProperties::BorderStyle::Dotted
                            ? qMax(.2,edit.box.borderWidthPt*.25)
                            : qMax(2.,edit.box.borderWidthPt*4.);
        const double gap=qMax(2.,edit.box.borderWidthPt*2.5);
        const auto edge=[&](double x1,double y1,double x2,double y2){
            const double len=std::hypot(x2-x1,y2-y1); if(len<=0)return;
            for(double at=0;at<len;at+=dash+gap){const double e=qMin(len,at+dash);FPDFPath_MoveTo(dashed,x1+(x2-x1)*at/len,y1+(y2-y1)*at/len);FPDFPath_LineTo(dashed,x1+(x2-x1)*e/len,y1+(y2-y1)*e/len);}
        };
        edge(left,bottom,right,bottom);edge(right,bottom,right,top);edge(right,top,left,top);edge(left,top,left,bottom);
        const QColor c=edit.box.borderColor;FPDFPageObj_SetStrokeColor(dashed,c.red(),c.green(),c.blue(),qRound(alpha*c.alphaF()));FPDFPageObj_SetStrokeWidth(dashed,qMax(.1,edit.box.borderWidthPt));FPDFPath_SetDrawMode(dashed,FPDF_FILLMODE_NONE,true);rotateObject(dashed,edit,pageHeight);FPDFPage_InsertObject(page,dashed);
    }
}

QStringList replacementLines(const EditSession::Edit &edit)
{
    QStringList lines = edit.newText.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        if (edit.box.listStyle == TextBoxProperties::ListStyle::Bullets)
            lines[i].prepend(QStringLiteral("• "));
        else if (edit.box.listStyle == TextBoxProperties::ListStyle::Numbered)
            lines[i].prepend(QString::number(i + 1) + QStringLiteral(". "));
    }
    return lines;
}

void insertReplacement(FPDF_DOCUMENT doc, FPDF_PAGE page,
                       const EditSession::Edit &edit, const Replaced *origin,
                       double pageHeight)
{
    if (edit.newText.isEmpty()) return;

    const QStringList lines = replacementLines(edit);

    double size = edit.fontSizePt > 0.0 ? edit.fontSizePt
                                        : qMax(6.0, edit.pdfBounds.height() * 0.8);
    if (!edit.sizeChanged && origin && origin->sizePt > 0.0)
        size = origin->sizePt;

    FPDF_FONT font = nullptr;
    bool ownsFont = false;
    if (!edit.fontChanged && origin && origin->font
            && fontCanRender(origin->font, lines.join(QChar(u' ')),
                             origin->text.isEmpty() ? edit.originalText
                                                    : origin->text)) {
        font = origin->font;
    } else {

        if (edit.fontChanged) {
            const QByteArray daten =
                PdfiumFonts::fontData(edit.fontFamily, edit.bold, edit.italic);
            if (!daten.isEmpty())
                font = FPDFText_LoadFont(
                    doc, reinterpret_cast<const uint8_t *>(daten.constData()),
                    static_cast<uint32_t>(daten.size()), FPDF_FONT_TRUETYPE, 1);
        }
        if (!font)
            font = FPDFText_LoadStandardFont(
                doc, standardFontLike(origin ? origin->font : nullptr,
                                      edit).constData());
        ownsFont = true;
    }
    if (!font) return;

    insertBoxDecoration(page, edit, pageHeight);

    const double links = firstBaseline(edit, size, pageHeight, 1).x();
    const double availableWidth = qMax(
        0.0, edit.pdfBounds.right() - edit.box.paddingPt - links);
    const auto glyphBreite = [font, size](QChar ch) {
        float advance = 0.f;
        if (ch.isLowSurrogate()
                || !FPDFFont_GetGlyphWidth(font, ch.unicode(),
                                           static_cast<float>(size), &advance))
            return 0.0;
        return double(advance);
    };
    QStringList umbrochen;
    for (const QString &line : lines)
        umbrochen.append(TextWrap::lines(line, availableWidth, glyphBreite,
                                         edit.box.characterSpacingPt));

    const QPointF start = firstBaseline(edit, size, pageHeight, umbrochen.size());
    const double  step  = (edit.lineSpacingPt > 0.0 ? edit.lineSpacingPt : size * 1.2)
                        + edit.box.paragraphSpacingPt;

    for (int i = 0; i < umbrochen.size(); ++i) {
        const QString &line = umbrochen.at(i);
        if (line.isEmpty()) continue;
        const QColor color = edit.textColor.isValid() ? edit.textColor : QColor(Qt::black);
        const auto insertObject = [&](const QString &text, double x) {
            FPDF_PAGEOBJECT obj=FPDFPageObj_CreateTextObj(doc,font,static_cast<float>(size));
            if(!obj)return;
            const std::u16string utf16=text.toStdU16String();
            FPDFText_SetText(obj,reinterpret_cast<FPDF_WIDESTRING>(utf16.c_str()));
            FPDFPageObj_SetFillColor(obj,color.red(),color.green(),color.blue(),
                                     qRound(color.alpha()*qBound(0.0,edit.box.opacity,1.0)));
            FPDFPageObj_Transform(obj,1,0,0,1,x,start.y()-step*i);
            rotateObject(obj,edit,pageHeight); FPDFPage_InsertObject(page,obj);
        };
        double lineX = start.x();
        if (edit.box.horizontalAlign != TextBoxProperties::HorizontalAlign::Left) {
            const double w = lineWidthPt(font, line, size, edit.box.characterSpacingPt);
            if (edit.box.horizontalAlign == TextBoxProperties::HorizontalAlign::Center)
                lineX += qMax(0.0, (availableWidth - w) / 2.0);
            else if (edit.box.horizontalAlign == TextBoxProperties::HorizontalAlign::Right)
                lineX += qMax(0.0, availableWidth - w);
        }
        if (qFuzzyIsNull(edit.box.characterSpacingPt)) {
            insertObject(line,lineX);
        } else {
            double x=lineX;
            for (const QChar ch : line) {
                insertObject(QString(ch),x);
                x += lineWidthPt(font, QString(ch), size, 0.0)
                   + edit.box.characterSpacingPt;
            }
        }
        if (!edit.underline) continue;

        const double breite = lineWidthPt(font, line, size,
                                          edit.box.characterSpacingPt);
        const double dicke  = qMax(0.3, size * 0.06);
        const double y      = start.y() - step * i - size * 0.12;
        FPDF_PAGEOBJECT rule = FPDFPageObj_CreateNewRect(lineX, y, breite, dicke);
        if (!rule) continue;
        FPDFPageObj_SetFillColor(rule, color.red(), color.green(), color.blue(),
                                 qRound(color.alpha()
                                        * qBound(0.0, edit.box.opacity, 1.0)));
        FPDFPath_SetDrawMode(rule, FPDF_FILLMODE_WINDING, false);
        rotateObject(rule, edit, pageHeight);
        FPDFPage_InsertObject(page, rule);
    }

    if (ownsFont) FPDFFont_Close(font);
}

void insertImage(FPDF_DOCUMENT doc, FPDF_PAGE page,
                 const EditSession::ImageEdit &edit, double pageHeight)
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
    FPDFImageObj_SetMatrix(obj, b.width(), 0, 0, b.height(),
                           b.left(), toPdfY(b.bottom(), pageHeight));
    FPDFPage_InsertObject(page, obj);
    FPDFBitmap_Destroy(bitmap);
}

void insertDrawStroke(FPDF_PAGE page, const EditSession::DrawStroke &stroke,
                      double pageHeight)
{
    if (stroke.points.isEmpty()) return;

    const QPointF first = stroke.points.first();
    FPDF_PAGEOBJECT path = FPDFPageObj_CreateNewPath(
        static_cast<float>(first.x()),
        static_cast<float>(toPdfY(first.y(), pageHeight)));
    if (!path) return;

    if (stroke.points.size() == 1) {
        FPDFPath_LineTo(path,
            static_cast<float>(first.x() + 0.01),
            static_cast<float>(toPdfY(first.y(), pageHeight)));
    } else {
        for (int i = 1; i < stroke.points.size(); ++i) {
            const QPointF point = stroke.points.at(i);
            FPDFPath_LineTo(path, static_cast<float>(point.x()),
                            static_cast<float>(toPdfY(point.y(), pageHeight)));
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

QRectF linkBounds(FPDF_ANNOTATION annot, double pageHeight)
{
    FS_RECTF rect {};
    if (!FPDFAnnot_GetRect(annot, &rect)) return {};
    return QRectF(rect.left, pageHeight - rect.top,
                  rect.right - rect.left, rect.top - rect.bottom);
}

bool sameBounds(const QRectF &a, const QRectF &b)
{
    constexpr double tolerance = 0.5;
    return std::abs(a.left() - b.left()) <= tolerance
        && std::abs(a.top() - b.top()) <= tolerance
        && std::abs(a.right() - b.right()) <= tolerance
        && std::abs(a.bottom() - b.bottom()) <= tolerance;
}

void setLinkRect(FPDF_ANNOTATION annot, const QRectF &bounds, double pageHeight)
{
    FS_RECTF rect {};
    rect.left   = static_cast<float>(bounds.left());
    rect.right  = static_cast<float>(bounds.right());
    rect.bottom = static_cast<float>(pageHeight - bounds.bottom());
    rect.top    = static_cast<float>(pageHeight - bounds.top());
    FPDFAnnot_SetRect(annot, &rect);
}

void appendLinkQuadPoints(FPDF_ANNOTATION annot, const QList<QRectF> &rects,
                          double pageHeight)
{
    for (const QRectF &rect : rects) {
        FS_QUADPOINTSF quad {};
        quad.x1 = static_cast<float>(rect.left());
        quad.y1 = static_cast<float>(pageHeight - rect.top());
        quad.x2 = static_cast<float>(rect.right());
        quad.y2 = quad.y1;
        quad.x3 = quad.x1;
        quad.y3 = static_cast<float>(pageHeight - rect.bottom());
        quad.x4 = quad.x2;
        quad.y4 = quad.y3;
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
                              double pageHeight)
{
    const float y = static_cast<float>(pageHeight - rect.bottom() + 0.8);
    FPDF_PAGEOBJECT path = FPDFPageObj_CreateNewPath(
        static_cast<float>(rect.left()), y);
    if (!path) return nullptr;
    FPDFPath_LineTo(path, static_cast<float>(rect.right()), y);
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
                     const EditSession &session, double pageHeight)
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
        const QRectF box(left, pageHeight - top, right - left, top - bottom);
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
        if (FPDF_PAGEOBJECT underline = blueUnderline(doc, rect, pageHeight))
            FPDFPage_InsertObject(page, underline);
}

void removeLinkStyle(FPDF_DOCUMENT doc, FPDF_PAGE page, const QList<QRectF> &rects,
                     double pageHeight)
{
    Q_UNUSED(doc)
    std::vector<FPDF_PAGEOBJECT> removed;
    const int count = FPDFPage_CountObjects(page);
    for (int i = 0; i < count; ++i) {
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, i);
        FPDF_PAGEOBJECTMARK mark = object ? linkStyleMark(object) : nullptr;
        if (!mark) continue;
        const QRectF bounds = objectBoundsQt(object, pageHeight);
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
                    const EditSession &session, double pageHeight)
{
    for (const EditSession::LinkEdit &edit : session.linkEdits()) {
        if (edit.page != pageIndex || !edit.existing) continue;

        if (edit.removed && edit.colorText)
            removeLinkStyle(doc, page,
                edit.textRects.isEmpty() ? QList<QRectF>{ edit.pdfBounds }
                                         : edit.textRects,
                pageHeight);

        const int count = FPDFPage_GetAnnotCount(page);
        for (int i = 0; i < count; ++i) {
            FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
            if (!annot) continue;
            const bool match = FPDFAnnot_GetSubtype(annot) == FPDF_ANNOT_LINK
                && uriOf(doc, annot) == edit.originalUrl
                && sameBounds(linkBounds(annot, pageHeight), edit.originalBounds);
            if (!match) {
                FPDFPage_CloseAnnot(annot);
                continue;
            }
            if (edit.removed) {
                FPDFPage_CloseAnnot(annot);
                FPDFPage_RemoveAnnot(page, i);
            } else {
                setLinkRect(annot, edit.pdfBounds, pageHeight);
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
        setLinkRect(annot, edit.pdfBounds, pageHeight);
        appendLinkQuadPoints(annot, edit.textRects, pageHeight);
        FPDFAnnot_SetBorder(annot, 0.0f, 0.0f, 0.0f);
        FPDFAnnot_SetURI(annot, edit.url.toUtf8().constData());
        FPDFPage_CloseAnnot(annot);
    }
}

}

void PdfiumEdits::applyToPage(FPDF_DOCUMENT doc, FPDF_PAGE page, int pageIndex,
                              const EditSession &session)
{
    if (!doc || !page) return;
    const double pageHeight = FPDF_GetPageHeightF(page);
    const QList<EditSession::Edit> &edits = session.edits();

    QList<Replaced> replaced;
    for (const EditSession::Edit &e : edits) {
        if (e.page != pageIndex || !e.formField.isEmpty()) continue;
        if (!removesOriginal(e)) continue;
        Replaced r;
        r.area = e.pdfBounds;
        r.text = e.originalText;
        removeTextIn(page, eraseAreas(e), pageHeight, r);
        replaced.append(std::move(r));
    }

    for (const EditSession::Edit &e : edits) {
        if (e.page != pageIndex || !e.formField.isEmpty()) continue;
        insertReplacement(doc, page, e, originOf(replaced, e), pageHeight);
    }

    for (const EditSession::ImageEdit &ie : session.imageEdits())
        if (ie.page == pageIndex) insertImage(doc, page, ie, pageHeight);

    for (const EditSession::DrawStroke &stroke : session.drawStrokes())
        if (stroke.page == pageIndex) insertDrawStroke(page, stroke, pageHeight);

    colorLinkedText(doc, page, pageIndex, session, pageHeight);
    applyLinkEdits(doc, page, pageIndex, session, pageHeight);

    for (Replaced &r : replaced)
        for (FPDF_PAGEOBJECT obj : r.removed) FPDFPageObj_Destroy(obj);
}

namespace {

void setAnnotationString(FPDF_ANNOTATION annotation, const char *key,
                         const QString &value)
{
    const std::u16string utf16 = value.toStdU16String();
    FPDFAnnot_SetStringValue(annotation, key,
        reinterpret_cast<FPDF_WIDESTRING>(utf16.c_str()));
}

QRectF noteBounds(FPDF_ANNOTATION annotation, double pageHeight)
{
    FS_RECTF rect {};
    if (!FPDFAnnot_GetRect(annotation, &rect)) return {};
    return QRectF(rect.left, pageHeight - rect.top,
                  rect.right - rect.left, rect.top - rect.bottom);
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

void setNoteRect(FPDF_ANNOTATION annotation, const QRectF &bounds,
                 double pageHeight)
{
    FS_RECTF rect;
    rect.left   = static_cast<float>(bounds.left());
    rect.right  = static_cast<float>(bounds.right());
    rect.top    = static_cast<float>(pageHeight - bounds.top());
    rect.bottom = static_cast<float>(pageHeight - bounds.bottom());
    FPDFAnnot_SetRect(annotation, &rect);
}

bool sameNoteBounds(const QRectF &a, const QRectF &b)
{
    return qAbs(a.left() - b.left()) < 0.5
        && qAbs(a.top() - b.top()) < 0.5
        && qAbs(a.width() - b.width()) < 0.5
        && qAbs(a.height() - b.height()) < 0.5;
}

}

void PdfiumEdits::applyNoteEdits(FPDF_PAGE page, int pageIndex,
                                 const EditSession &session)
{
    if (!page) return;
    const double pageHeight = FPDF_GetPageHeightF(page);

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
                    && sameNoteBounds(noteBounds(annotation, pageHeight),
                                      edit.originalBounds)));
            if (!matches) {
                FPDFPage_CloseAnnot(annotation);
                continue;
            }
            if (edit.removed) {
                FPDFPage_CloseAnnot(annotation);
                FPDFPage_RemoveAnnot(page, i);
            } else {
                setNoteRect(annotation, edit.pdfBounds, pageHeight);
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
        setNoteRect(annotation, edit.pdfBounds, pageHeight);
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
