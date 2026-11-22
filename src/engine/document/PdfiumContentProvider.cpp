#include "engine/document/PdfiumContentProvider.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumLock.hpp"
#include "engine/document/PdfiumPageSpace.hpp"
#include "engine/document/PdfiumTextRules.hpp"
#include "engine/edit/ContentMap.hpp"

#include "fpdf_annot.h"
#include "fpdf_edit.h"
#include "fpdf_text.h"

#include <QColor>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

template <typename Fn>
QString utf16Value(Fn &&call)
{
    const unsigned long bytes = call(nullptr, 0);
    if (bytes <= 2) return {};
    std::vector<unsigned short> buffer(bytes / 2 + 1, 0);
    call(buffer.data(), bytes);
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.data()));
}

QRectF toTopLeft(const FS_RECTF &r, const PdfiumPageSpace &space)
{
    return space.toView(r.left, r.bottom, r.right, r.top);
}

QColor fillColorAt(FPDF_TEXTPAGE tp, int index)
{
    unsigned int r = 0, g = 0, b = 0, a = 0;
    if (!FPDFText_GetFillColor(tp, index, &r, &g, &b, &a)) return {};
    if (a == 0) return {};
    // Text set gray as black under an opacity keeps it, so the editor shows it gray.
    return QColor(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b), static_cast<int>(a));
}

QString annotString(FPDF_ANNOTATION annot, const char *key)
{
    return utf16Value([&](unsigned short *buf, unsigned long len) {
        return FPDFAnnot_GetStringValue(annot, key, buf, len);
    });
}

double fontSizeFromDA(const QString &da)
{
    const QStringList tokens = da.split(QRegularExpression(QStringLiteral("\\s+")),
                                        Qt::SkipEmptyParts);
    for (int i = 1; i < tokens.size(); ++i) {
        if (tokens.at(i) != QLatin1String("Tf")) continue;
        bool ok = false;
        const double size = tokens.at(i - 1).toDouble(&ok);
        if (ok && size > 0.0) return size;
    }
    return 0.0;
}

QColor colorFromDA(const QString &da)
{
    const QStringList tokens = da.split(QRegularExpression(QStringLiteral("\\s+")),
                                        Qt::SkipEmptyParts);
    const auto value = [&tokens](qsizetype i) { return qBound(0.0, tokens.at(i).toDouble(), 1.0); };
    for (qsizetype i = tokens.size() - 1; i >= 1; --i) {
        const QString &op = tokens.at(i);
        if (op == QLatin1String("g"))
            return QColor::fromRgbF(value(i - 1), value(i - 1), value(i - 1));
        if (op == QLatin1String("rg") && i >= 3)
            return QColor::fromRgbF(value(i - 3), value(i - 2), value(i - 1));
        if (op == QLatin1String("k") && i >= 4)
            return QColor::fromCmykF(value(i - 4), value(i - 3), value(i - 2), value(i - 1));
    }
    return {};
}

QString fontNameAt(FPDF_TEXTPAGE tp, int index)
{
    int flags = 0;
    const unsigned long bytes = FPDFText_GetFontInfo(tp, index, nullptr, 0, &flags);
    if (bytes == 0) return {};
    std::vector<char> buffer(bytes + 1, 0);
    FPDFText_GetFontInfo(tp, index, buffer.data(), bytes, &flags);
    return QString::fromUtf8(buffer.data());
}

}

PdfiumContentProvider::PdfiumContentProvider(FPDF_DOCUMENT doc, ScanText scanText)
    : m_doc(doc)
    , m_scanText(std::move(scanText))
{
}

PdfiumContentProvider::~PdfiumContentProvider() = default;

QList<ContentItem> PdfiumContentProvider::buildPage(int page)
{
    return buildPageItems(page, true);
}

QList<ContentItem> PdfiumContentProvider::pageItemsForExport(int page)
{

    return buildPageItems(page, false);
}

QList<ContentItem> PdfiumContentProvider::buildPageItems(int page,
                                                        bool mergeVertical)
{
    PdfiumLock lock;
    if (!m_doc) return {};
    FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
    if (!pg) return {};

    const PdfiumPageSpace space(pg);

    QList<ContentItem> fields;
    QList<ContentItem> media;
    collectAnnotations(pg, space, &fields, &media);

    // Turned text is read in its own direction and turned back onto the page.
    QList<ContentItem> text;
    const auto read = [&](FPDF_TEXTPAGE tp, const PdfiumPageSpace &in, const QList<QRectF> &boxes) {
        double slope = 0.0;
        const QMap<int, QList<ContentCluster>> words = collectWords(tp, in, mergeVertical, boxes, &slope);
        for (auto it = words.cbegin(); it != words.cend(); ++it) {
            // An OCR layer over a page scanned askew was read level, turned back
            // by the page's slope; its items turn with the page again.
            const QTransform turn = QTransform().rotate(it.key())
                                  * (it.key() == 0 ? QTransform(1.0, -slope, slope, 1.0, 0.0, 0.0).inverted() : QTransform());
            for (ContentItem item : classifyContentClusters(it.value(), mergeVertical)) {
                item.bounds     = turn.mapRect(item.bounds);
                item.textOrigin = turn.map(item.textOrigin);
                item.turn       = it.key();
                text.append(item);
            }
        }
    };
    // A scan that OCR read is edited as the text it holds.
    FPDF_PAGE scanPage = nullptr;
    FPDF_TEXTPAGE scanText = nullptr;
    QList<QRectF> scanBoxes;
    if (mergeVertical && m_scanText && m_scanText(page, scanPage, scanText, scanBoxes)) {
        read(scanText, PdfiumPageSpace(scanPage), scanBoxes);
    } else if (FPDF_TEXTPAGE tp = FPDFText_LoadPage(pg)) {
        read(tp, space, {});
        FPDFText_ClosePage(tp);
    }
    FPDF_ClosePage(pg);

    QList<ContentItem> result;
    result.reserve(fields.size() + text.size() + media.size());
    result += fields;
    result += text;
    result += media;
    return result;
}

QMap<int, QList<ContentCluster>> PdfiumContentProvider::collectWords(FPDF_TEXTPAGE tp,
                                                                     const PdfiumPageSpace &space,
                                                                     bool byTurn,
                                                                     const QList<QRectF> &boxes,
                                                                     double *slope) const
{
    QMap<int, QList<ContentCluster>> words;
    const int count = FPDFText_CountChars(tp);

    // An OCR layer over a page scanned a little askew slopes with it, each
    // word by its matrix, as PDFium's character angle leans with how far a
    // word is squeezed: it is read level, sheared by the page's slope.
    constexpr double kAskew = 0.0524;
    const auto tiltOf = [&](int i) {
        FPDF_PAGEOBJECT object = FPDFText_GetTextObject(tp, i);
        FS_MATRIX m;
        if (!object || FPDFTextObj_GetTextRenderMode(object) != FPDF_TEXTRENDERMODE_INVISIBLE
                || !FPDFText_GetMatrix(tp, i, &m) || (m.a == 0.0f && m.b == 0.0f))
            return qQNaN();
        const double shown = space.shownAngle(std::fmod(2.0 * M_PI - std::atan2(double(m.b), double(m.a)), 2.0 * M_PI));
        const double tilt = shown > M_PI ? shown - 2.0 * M_PI : shown;
        return std::abs(tilt) <= kAskew ? tilt : qQNaN();
    };
    QList<double> slopes;
    for (int i = 0; byTurn && i < count && boxes.isEmpty(); ++i)
        if (const double tilt = tiltOf(i); !std::isnan(tilt) && tilt != 0.0) slopes.append(std::tan(tilt));
    // What the app's OCR read is set level line by line, its characters'
    // boxes where OCR found them: the slope is that of the letters standing
    // on the baseline along each long line.
    QHash<FPDF_PAGEOBJECT, QList<QPointF>> feet;
    for (int i = 0; byTurn && i < count && i < boxes.size(); ++i) {
        const QChar ch(static_cast<char16_t>(FPDFText_GetUnicode(tp, i)));
        const QRectF &box = boxes.at(i);
        if (!box.isEmpty() && (ch.isUpper() || ch.isDigit() || QStringLiteral("acemnorsuvwxz").contains(ch)))
            feet[FPDFText_GetTextObject(tp, i)].append(QPointF(box.center().x(), box.bottom()));
    }
    for (const QList<QPointF> &line : std::as_const(feet)) {
        if (line.size() < 6 || line.last().x() - line.first().x() < 100.0) continue;
        double mx = 0.0, my = 0.0;
        for (const QPointF &f : line) { mx += f.x(); my += f.y(); }
        mx /= line.size(); my /= line.size();
        double sxy = 0.0, sxx = 0.0;
        for (const QPointF &f : line) { sxy += (f.x() - mx) * (f.y() - my); sxx += (f.x() - mx) * (f.x() - mx); }
        if (sxx > 0.0 && std::abs(sxy / sxx) <= std::tan(kAskew)) slopes.append(sxy / sxx);
    }
    std::sort(slopes.begin(), slopes.end());
    const double shear = slopes.isEmpty() ? 0.0 : slopes.at(slopes.size() / 2);
    if (slope) *slope = shear;

    ContentCluster current;
    bool           open = false;
    int            currentTurn = 0;
    QRectF         lastBox;
    double         lastBaseline = 0.0;

    const auto flush = [&]() {
        if (open && !current.text.trimmed().isEmpty() && current.bounds.isValid())
            words[currentTurn].append(current);
        open = false;
        current = ContentCluster{};
    };

    for (int i = 0; i < count; ++i) {
        double left = 0, right = 0, bottom = 0, top = 0;
        if (!FPDFText_GetCharBox(tp, i, &left, &right, &bottom, &top)) continue;

        const QChar ch(static_cast<char16_t>(FPDFText_GetUnicode(tp, i)));
        if (ch.isNull()) continue;
        // Text turned otherwise than by whole quarters cannot be edited.
        const bool askew = byTurn && !std::isnan(tiltOf(i));
        const int turn = byTurn && !askew ? space.quarterTurn(FPDFText_GetCharAngle(tp, i)) : 0;
        if (turn < 0) { flush(); continue; }
        if (ch.isSpace() || (open && turn != currentTurn)) flush();
        if (ch.isSpace()) continue;

        const QTransform unturn = QTransform().rotate(-turn);
        double originX = 0, originY = 0;
        FPDFText_GetCharOrigin(tp, i, &originX, &originY);
        QRectF box = unturn.mapRect(space.toView(left, bottom, right, top));
        const QPointF origin = unturn.map(space.toView(originX, originY));
        originX = origin.x();
        // A scanned character stands where OCR found it; its line, set level,
        // stands where OCR found it in its middle.
        const QRectF scanned = boxes.value(i);
        double level = askew ? originX * shear : 0.0;
        if (!scanned.isEmpty()) {
            box     = scanned;
            originX = scanned.left();
            box.translate(0.0, -box.center().x() * shear);
            float l = 0, b = 0, r = 0, t = 0;
            if (FPDF_PAGEOBJECT obj = FPDFText_GetTextObject(tp, i); obj && FPDFPageObj_GetBounds(obj, &l, &b, &r, &t))
                level = (space.toView(l, b).x() + space.toView(r, t).x()) / 2.0 * shear;
        } else if (askew) {
            box.translate(0.0, -originX * shear);
        }
        const double baseline = origin.y() - level;
        const double fontSize = PdfiumTextRules::effectiveFontSize(tp, i);
        // A page lying askew is turned, not sheared: its margins lean as its
        // lines slope, so each line is also set back along itself.
        const double along = askew || !scanned.isEmpty() ? baseline * shear : 0.0;
        box.translate(along, 0.0);
        originX += along;

        if (open && (!PdfiumTextRules::sameLine(baseline, lastBaseline, box.height())
                     || PdfiumTextRules::separatesWords(lastBox, box, fontSize)))
            flush();

        if (!open) {
            current.bounds        = box;
            current.text          = QString(ch);

            current.fontSizePt    = fontSize > 0.0 ? fontSize : qMax(2.0, box.height());
            current.fontSizeExact = fontSize > 0.0;
            current.rawFontName   = fontNameAt(tp, i);
            current.textColor     = fillColorAt(tp, i);
            current.exactWidth    = true;
            current.origin        = QPointF(originX, baseline);
            current.read          = askew || !boxes.value(i).isEmpty();
            if (FPDF_PAGEOBJECT obj = FPDFText_GetTextObject(tp, i)) {
                float l = 0, b = 0, r = 0, t = 0;
                if (FPDFPageObj_GetBounds(obj, &l, &b, &r, &t))
                    current.objectBounds = unturn.mapRect(space.toView(l, b, r, t))
                                               .translated(along, askew ? -(originX - along) * shear : 0.0);
            }
            open = true;
            currentTurn = turn;
        } else {
            current.bounds = current.bounds.united(box);
            current.text  += ch;
        }
        lastBox      = box;
        lastBaseline = baseline;
    }
    flush();
    return words;
}

void PdfiumContentProvider::collectAnnotations(FPDF_PAGE pg, const PdfiumPageSpace &space,
                                               QList<ContentItem> *fields,
                                               QList<ContentItem> *media) const
{
    const int count = FPDFPage_GetAnnotCount(pg);
    for (int i = 0; i < count; ++i) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(pg, i);
        if (!annot) continue;

        FS_RECTF rect {};
        const bool haveRect = FPDFAnnot_GetRect(annot, &rect);
        const int  subtype  = FPDFAnnot_GetSubtype(annot);

        if (haveRect && subtype == FPDF_ANNOT_WIDGET) {

            if (annotString(annot, "FT") == QLatin1String("Tx")) {
                ContentItem item;
                item.type      = ContentItem::Type::FormField;
                item.bounds    = toTopLeft(rect, space);
                item.fieldName = annotString(annot, "T");
                item.text      = annotString(annot, "V");
                const double size = fontSizeFromDA(annotString(annot, "DA"));
                if (size > 0.0) {
                    item.fontSizePt    = size;
                    item.fontSizeExact = true;
                }
                item.textColor = colorFromDA(annotString(annot, "DA"));
                if (item.isValid()) fields->append(std::move(item));
            }
        } else if (haveRect && (subtype == FPDF_ANNOT_SCREEN
                                || subtype == FPDF_ANNOT_MOVIE
                                || subtype == FPDF_ANNOT_RICHMEDIA)) {

            ContentItem item;
            item.type   = ContentItem::Type::Media;
            item.bounds = toTopLeft(rect, space);
            if (item.isValid()) media->append(std::move(item));
        }

        FPDFPage_CloseAnnot(annot);
    }
}

#endif
