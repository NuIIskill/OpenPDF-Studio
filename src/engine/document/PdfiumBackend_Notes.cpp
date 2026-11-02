#include "engine/document/PdfiumBackend.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumLock.hpp"
#include "engine/document/PdfiumPageSpace.hpp"

#include "fpdf_annot.h"
#include "fpdf_text.h"

#include <vector>

namespace {

QString annotationString(FPDF_ANNOTATION annotation, const char *key)
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

QString textOf(FPDF_TEXTPAGE text, int start, int count)
{
    if (count <= 0) return {};
    std::vector<unsigned short> buffer(static_cast<size_t>(count) + 1, 0);
    const int written = FPDFText_GetText(text, start, count, buffer.data());
    if (written <= 1) return {};
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.data()), written - 1);
}

QString boundedText(FPDF_TEXTPAGE text, const QRectF &rect, const PdfiumPageSpace &space)
{
    const QRectF pdf = space.toPdf(rect);
    const double left = pdf.left(), right = pdf.right();
    const double top = pdf.bottom(), bottom = pdf.top();
    const int chars = FPDFText_GetBoundedText(text, left, top, right, bottom, nullptr, 0);
    if (chars <= 0) return {};
    std::vector<unsigned short> buffer(static_cast<size_t>(chars) + 1, 0);
    const int written = FPDFText_GetBoundedText(text, left, top, right, bottom,
                                                buffer.data(), chars + 1);
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.data()),
                              qMax(0, written)).remove(QChar(0));
}

QList<QRectF> highlightRects(FPDF_ANNOTATION annotation, const PdfiumPageSpace &space)
{
    QList<QRectF> rects;
    const size_t count = FPDFAnnot_CountAttachmentPoints(annotation);
    for (size_t i = 0; i < count; ++i) {
        FS_QUADPOINTSF q {};
        if (!FPDFAnnot_GetAttachmentPoints(annotation, i, &q)) continue;
        const double left   = qMin(qMin(q.x1, q.x2), qMin(q.x3, q.x4));
        const double right  = qMax(qMax(q.x1, q.x2), qMax(q.x3, q.x4));
        const double bottom = qMin(qMin(q.y1, q.y2), qMin(q.y3, q.y4));
        const double top    = qMax(qMax(q.y1, q.y2), qMax(q.y3, q.y4));
        rects.append(space.toView(left, bottom, right, top));
    }
    return rects;
}

}

QList<PdfBackend::Note> PdfiumBackend::pageNotes(int page) const
{
    PdfiumLock lock;
    QList<Note> result;
    if (!m_doc || page < 0 || page >= pageCount()) return result;

    FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
    if (!pg) return result;
    const PdfiumPageSpace space(pg);
    FPDF_TEXTPAGE text = nullptr;

    const int count = FPDFPage_GetAnnotCount(pg);
    for (int i = 0; i < count; ++i) {
        FPDF_ANNOTATION annotation = FPDFPage_GetAnnot(pg, i);
        if (!annotation) continue;
        const int subtype = FPDFAnnot_GetSubtype(annotation);
        // A highlight counts as a note when it carries a comment or was made as one here.
        const bool highlight = subtype == FPDF_ANNOT_HIGHLIGHT
            && (annotationString(annotation, "OpenPDFNote") == QLatin1String("1")
                || !annotationString(annotation, "Contents").isEmpty());
        FS_RECTF rect {};
        if ((subtype == FPDF_ANNOT_TEXT || highlight)
                && FPDFAnnot_GetRect(annotation, &rect)) {
            Note note;
            note.id     = annotationString(annotation, "NM");
            note.title  = annotationString(annotation, "T");
            note.text   = annotationString(annotation, "Contents");
            note.pinned = annotationString(annotation, "OpenPDFPinned")
                       == QLatin1String("1");
            note.bounds = space.toView(rect.left, rect.bottom, rect.right, rect.top);
            unsigned int r = 0, g = 0, b = 0, a = 0;
            if (FPDFAnnot_GetColor(annotation, FPDFANNOT_COLORTYPE_Color, &r, &g, &b, &a))
                note.color = QColor(int(r), int(g), int(b));
            if (highlight) {
                note.textRects = highlightRects(annotation, space);
                if (!text) text = FPDFText_LoadPage(pg);
                QStringList parts;
                for (const QRectF &r : std::as_const(note.textRects))
                    if (text) parts << boundedText(text, r, space).simplified();
                note.quote = parts.join(u' ').simplified();
            }
            if (note.bounds.isValid() && (!highlight || !note.textRects.isEmpty()))
                result.append(std::move(note));
        }
        FPDFPage_CloseAnnot(annotation);
    }
    if (text) FPDFText_ClosePage(text);
    FPDF_ClosePage(pg);
    return result;
}

PdfBackend::Selection PdfiumBackend::sentenceAt(int page, const QPointF &pdfPt) const
{
    PdfiumLock lock;
    Selection out;
    if (!m_doc || page < 0 || page >= pageCount()) return out;
    FPDF_PAGE pg = readPage(page);
    FPDF_TEXTPAGE text = pg ? readText(page) : nullptr;
    if (!text) return out;
    const PdfiumPageSpace space(pg);
    const QPointF at = space.toPdf(pdfPt);
    const int hit = FPDFText_GetCharIndexAtPos(text, at.x(), at.y(), 3.0, 3.0);
    const int chars = FPDFText_CountChars(text);
    if (hit < 0 || hit >= chars) return out;

    const auto ch = [text](int i) { return QChar(char16_t(FPDFText_GetUnicode(text, i))); };
    const auto ends = [&](int i) {
        const QChar c = ch(i);
        return (c == u'.' || c == u'!' || c == u'?' || c == u'…')
            && (i + 1 >= chars || ch(i + 1).isSpace());
    };
    // A line break starts a new paragraph when the next line sits further
    // down than a line's spacing or is set in another size.
    const auto paragraphBreak = [&](int i) {
        if (ch(i) != u'\n' && ch(i) != u'\r') return false;
        int before = i - 1, after = i + 1;
        while (before >= 0 && ch(before).isSpace()) --before;
        while (after < chars && ch(after).isSpace()) ++after;
        if (before < 0 || after >= chars) return true;
        double x = 0, above = 0, below = 0;
        if (!FPDFText_GetCharOrigin(text, before, &x, &above)
                || !FPDFText_GetCharOrigin(text, after, &x, &below))
            return true;
        const double size = FPDFText_GetFontSize(text, before);
        const double next = FPDFText_GetFontSize(text, after);
        if (size <= 0 || qAbs(next - size) > size * 0.15) return true;
        return qAbs(above - below) > size * 1.9;
    };
    constexpr int kLongest = 600;
    int first = hit;
    for (int i = hit - 1; i >= 0 && hit - i < kLongest; --i) {
        if (ends(i) || paragraphBreak(i)) break;
        first = i;
    }
    int last = hit;
    for (int i = hit; i < chars && i - hit < kLongest; ++i) {
        if (paragraphBreak(i)) break;
        last = i;
        if (ends(i)) break;
    }
    while (first < last && ch(first).isSpace()) ++first;
    while (last > first && ch(last).isSpace()) --last;
    if (ch(first).isSpace()) return out;

    const int count = last - first + 1;
    const int rects = FPDFText_CountRects(text, first, count);
    for (int i = 0; i < rects; ++i) {
        double left = 0, top = 0, right = 0, bottom = 0;
        if (FPDFText_GetRect(text, i, &left, &top, &right, &bottom))
            out.rects.append(space.toView(left, bottom, right, top));
    }
    out.text = textOf(text, first, count).simplified();
    return out;
}

#endif
