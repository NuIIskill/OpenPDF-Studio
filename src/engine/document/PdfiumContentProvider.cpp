#include "engine/document/PdfiumContentProvider.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumTextRules.hpp"
#include "engine/edit/ContentMap.hpp"

#include "fpdf_annot.h"
#include "fpdf_text.h"

#include <QColor>
#include <QRegularExpression>
#include <QStringList>

#include <vector>

namespace {

/// Text eines PDFium-Aufrufs, der UTF-16 in einen Puffer schreibt und dessen
/// Größe in BYTES zurückgibt — inklusive abschließender Null. Diese Konvention
/// ist bei jedem der Aufrufe gleich und einmal an einer Stelle zu behandeln
/// besser als fünfmal daneben.
template <typename Fn>
QString utf16Value(Fn &&call)
{
    const unsigned long bytes = call(nullptr, 0);
    if (bytes <= 2) return {};                  // nur die Null
    std::vector<unsigned short> buffer(bytes / 2 + 1, 0);
    call(buffer.data(), bytes);
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.data()));
}

/// PDF rechnet von unten, das Regionenmodell von oben.
QRectF toTopLeft(const FS_RECTF &r, double pageHeight)
{
    return QRectF(r.left, pageHeight - r.top,
                  r.right - r.left, r.top - r.bottom);
}

QColor fillColorAt(FPDF_TEXTPAGE tp, int index)
{
    unsigned int r = 0, g = 0, b = 0, a = 0;
    if (!FPDFText_GetFillColor(tp, index, &r, &g, &b, &a)) return {};
    if (a == 0) return {};
    return QColor(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
}

/// Zeichenketten-Eintrag aus dem Wörterbuch einer Annotation.
QString annotString(FPDF_ANNOTATION annot, const char *key)
{
    return utf16Value([&](unsigned short *buf, unsigned long len) {
        return FPDFAnnot_GetStringValue(annot, key, buf, len);
    });
}

/// Schriftgröße aus dem /DA eines Formularfeldes, etwa "/Helv 12 Tf 0 g".
/// Gesucht ist die Zahl unmittelbar vor dem Operator "Tf"; 0 heißt "automatisch"
/// und gilt hier als unbekannt.
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

QString fontNameAt(FPDF_TEXTPAGE tp, int index)
{
    int flags = 0;
    const unsigned long bytes = FPDFText_GetFontInfo(tp, index, nullptr, 0, &flags);
    if (bytes == 0) return {};
    std::vector<char> buffer(bytes + 1, 0);
    FPDFText_GetFontInfo(tp, index, buffer.data(), bytes, &flags);
    return QString::fromUtf8(buffer.data());
}

} // namespace

PdfiumContentProvider::PdfiumContentProvider(FPDF_DOCUMENT doc)
    : m_doc(doc)
{
}

PdfiumContentProvider::~PdfiumContentProvider() = default;

QList<ContentItem> PdfiumContentProvider::buildPage(int page)
{
    return buildPageItems(page, true);
}

QList<ContentItem> PdfiumContentProvider::pageItemsForExport(int page)
{
    // Keep one item per visual line/cell, as the 0.2.5 export path did. The
    // DOCX layout pass needs those baselines to reconstruct row pitch and
    // wrapped cells; merged editor paragraphs discard that information.
    return buildPageItems(page, false);
}

QList<ContentItem> PdfiumContentProvider::buildPageItems(int page,
                                                        bool mergeVertical)
{
    if (!m_doc) return {};
    FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
    if (!pg) return {};

    const double pageHeight = FPDF_GetPageHeightF(pg);

    QList<ContentItem> fields;
    QList<ContentItem> media;
    collectAnnotations(pg, pageHeight, &fields, &media);

    QList<ContentItem> text;
    if (FPDF_TEXTPAGE tp = FPDFText_LoadPage(pg)) {
        text = classifyContentClusters(collectWords(tp, pageHeight), mergeVertical);
        FPDFText_ClosePage(tp);
    }
    FPDF_ClosePage(pg);

    // Reihenfolge wie im Poppler-Pfad: Felder zuerst, damit ein Klick in ein
    // Formularfeld dieses trifft und nicht den Text, der darin steht.
    QList<ContentItem> result;
    result.reserve(fields.size() + text.size() + media.size());
    result += fields;
    result += text;
    result += media;
    return result;
}

QList<ContentCluster> PdfiumContentProvider::collectWords(FPDF_TEXTPAGE tp,
                                                          double pageHeight) const
{
    QList<ContentCluster> words;
    const int count = FPDFText_CountChars(tp);

    ContentCluster current;
    bool           open = false;
    QRectF         lastBox;
    double         lastBaseline = 0.0;

    const auto flush = [&]() {
        if (open && !current.text.trimmed().isEmpty() && current.bounds.isValid())
            words.append(current);
        open = false;
        current = ContentCluster{};
    };

    for (int i = 0; i < count; ++i) {
        double left = 0, right = 0, bottom = 0, top = 0;
        if (!FPDFText_GetCharBox(tp, i, &left, &right, &bottom, &top)) continue;

        const QChar ch(static_cast<char16_t>(FPDFText_GetUnicode(tp, i)));
        if (ch.isNull()) continue;
        if (ch.isSpace()) { flush(); continue; }   // Leerzeichen trennen Wörter

        double originX = 0, originY = 0;
        FPDFText_GetCharOrigin(tp, i, &originX, &originY);
        const QRectF box(left, pageHeight - top, right - left, top - bottom);
        const double baseline = pageHeight - originY;
        const double fontSize = FPDFText_GetFontSize(tp, i);

        // Ein Zeilenwechsel oder eine Lücke ohne Leerzeichen beendet das Wort.
        if (open && (!PdfiumTextRules::sameLine(baseline, lastBaseline, box.height())
                     || PdfiumTextRules::separatesWords(lastBox, box, fontSize)))
            flush();

        if (!open) {
            current.bounds        = box;
            current.text          = QString(ch);
            // Die Schriftgröße steht im Dokument und muss nicht aus Kastenhöhen
            // geschätzt werden — der Unterschied zu beiden Vorgängern.
            current.fontSizePt    = fontSize > 0.0 ? fontSize : qMax(2.0, box.height());
            current.fontSizeExact = fontSize > 0.0;
            current.rawFontName   = fontNameAt(tp, i);
            current.textColor     = fillColorAt(tp, i);
            current.exactWidth    = true;
            current.origin        = QPointF(originX, baseline);
            open = true;
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

void PdfiumContentProvider::collectAnnotations(FPDF_PAGE pg, double pageHeight,
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
            // Nur Textfelder: Knöpfe und Auswahllisten kann der Inline-Editor
            // nicht bearbeiten, und als anklickbare Region wären sie im Weg.
            if (annotString(annot, "FT") == QLatin1String("Tx")) {
                ContentItem item;
                item.type      = ContentItem::Type::FormField;
                item.bounds    = toTopLeft(rect, pageHeight);
                item.fieldName = annotString(annot, "T");
                item.text      = annotString(annot, "V");
                const double size = fontSizeFromDA(annotString(annot, "DA"));
                if (size > 0.0) {
                    item.fontSizePt    = size;   // aus dem /DA des Feldes — exakt
                    item.fontSizeExact = true;
                }
                if (item.isValid()) fields->append(std::move(item));
            }
        } else if (haveRect && (subtype == FPDF_ANNOT_SCREEN
                                || subtype == FPDF_ANNOT_MOVIE
                                || subtype == FPDF_ANNOT_RICHMEDIA)) {
            // Medien werden erkannt, damit der Editor sie in Ruhe lässt.
            ContentItem item;
            item.type   = ContentItem::Type::Media;
            item.bounds = toTopLeft(rect, pageHeight);
            if (item.isValid()) media->append(std::move(item));
        }

        FPDFPage_CloseAnnot(annot);
    }
}

#endif // HAVE_PDF_RENDERING && HAVE_PDFIUM
