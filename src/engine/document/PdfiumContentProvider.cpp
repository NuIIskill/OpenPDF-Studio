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
#include <QRegularExpression>
#include <QStringList>
#include <QTransform>

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
    return QColor(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
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
        const QMap<int, QList<ContentCluster>> words = collectWords(tp, in, mergeVertical, boxes);
        for (auto it = words.cbegin(); it != words.cend(); ++it) {
            const QTransform turn = QTransform().rotate(it.key());
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
                                                                     const QList<QRectF> &boxes) const
{
    QMap<int, QList<ContentCluster>> words;
    const int count = FPDFText_CountChars(tp);

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
        const int turn = byTurn ? space.quarterTurn(FPDFText_GetCharAngle(tp, i)) : 0;
        if (turn < 0) { flush(); continue; }
        if (ch.isSpace() || (open && turn != currentTurn)) flush();
        if (ch.isSpace()) continue;

        const QTransform unturn = QTransform().rotate(-turn);
        double originX = 0, originY = 0;
        FPDFText_GetCharOrigin(tp, i, &originX, &originY);
        QRectF box = unturn.mapRect(space.toView(left, bottom, right, top));
        const QPointF origin = unturn.map(space.toView(originX, originY));
        originX = origin.x();
        // A scanned character stands where OCR found it.
        if (const QRectF scanned = boxes.value(i); !scanned.isEmpty()) {
            box     = scanned;
            originX = scanned.left();
        }
        const double baseline = origin.y();
        const double fontSize = PdfiumTextRules::effectiveFontSize(tp, i);

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
            if (FPDF_PAGEOBJECT obj = FPDFText_GetTextObject(tp, i)) {
                float l = 0, b = 0, r = 0, t = 0;
                if (FPDFPageObj_GetBounds(obj, &l, &b, &r, &t))
                    current.objectBounds = unturn.mapRect(space.toView(l, b, r, t));
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
