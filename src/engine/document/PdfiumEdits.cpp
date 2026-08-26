#include "engine/document/PdfiumEdits.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/edit/EditSession.hpp"

#include "fpdf_edit.h"

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
    return edit.newText.isNull() || !edit.eraseRects.isEmpty();
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

QByteArray standardFontFor(const QString &family, bool bold, bool italic)
{
    const QString f = family.toLower();
    const bool mono  = f.contains(QLatin1String("mono"))
                    || f.contains(QLatin1String("courier"))
                    || f.contains(QLatin1String("consol"));
    const bool serif = !mono
                    && (f.contains(QLatin1String("serif"))
                        || f.contains(QLatin1String("times"))
                        || f.contains(QLatin1String("georgia"))
                        || f.contains(QLatin1String("garamond"))
                        || f.contains(QLatin1String("book"))
                        || f.contains(QLatin1String("roman"))
                        || f.contains(QLatin1String("minion"))
                        || f.contains(QLatin1String("cambria")))
                    && !f.contains(QLatin1String("sans"));

    if (mono)
        return bold ? (italic ? "Courier-BoldOblique" : "Courier-Bold")
                    : (italic ? "Courier-Oblique"     : "Courier");
    if (serif)
        return bold ? (italic ? "Times-BoldItalic" : "Times-Bold")
                    : (italic ? "Times-Italic"     : "Times-Roman");
    return bold ? (italic ? "Helvetica-BoldOblique" : "Helvetica-Bold")
                : (italic ? "Helvetica-Oblique"     : "Helvetica");
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
    return standardFontFor(family, bold, italic);
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


QPointF firstBaseline(const EditSession::Edit &edit, double fontSize, double pageHeight)
{
    const bool customInset = edit.box.paddingPt > 0.0
                          || edit.box.verticalAlign != TextBoxProperties::VerticalAlign::Top;
    if (edit.hasTextOrigin && !customInset) {
        const QPointF qt = edit.pdfBounds.topLeft() + edit.textOriginOffset;
        return QPointF(qt.x(), toPdfY(qt.y(), pageHeight));
    }
    const int lines = qMax(1, edit.newText.count(u'\n') + 1);
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
        font = FPDFText_LoadStandardFont(
            doc, standardFontLike(origin ? origin->font : nullptr, edit).constData());
        ownsFont = true;
    }
    if (!font) return;

    insertBoxDecoration(page, edit, pageHeight);

    const QPointF start = firstBaseline(edit, size, pageHeight);
    const double  step  = (edit.lineSpacingPt > 0.0 ? edit.lineSpacingPt : size * 1.2)
                        + edit.box.paragraphSpacingPt;
    const double availableWidth = qMax(0.0, edit.pdfBounds.width()
        - 2 * edit.box.paddingPt - edit.box.indentLevel * 18.0);

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
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

} // namespace

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

    for (Replaced &r : replaced)
        for (FPDF_PAGEOBJECT obj : r.removed) FPDFPageObj_Destroy(obj);
}

#endif
