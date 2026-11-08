#include "engine/document/PdfiumScanText.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumFonts.hpp"
#include "fpdf_edit.h"

#include <QFont>
#include <QFontDatabase>
#include <QStringList>

#include <algorithm>
#include <string>

namespace {

// Faces covering many scripts; a line is set in the first that holds all of its characters.
const QStringList &faces()
{
    static const QStringList families {
        QStringLiteral("DejaVu Sans"), QStringLiteral("Noto Sans"), QStringLiteral("Liberation Sans"),
        QStringLiteral("Arial"), QStringLiteral("Segoe UI"), QStringLiteral("Arial Unicode MS"),
        QStringLiteral("Noto Sans CJK SC"), QStringLiteral("Droid Sans Fallback"),
        QStringLiteral("Microsoft YaHei"), QStringLiteral("MS Gothic"), QStringLiteral("Malgun Gothic"),
        QStringLiteral("SimSun"),
    };
    return families;
}

double median(QList<double> values)
{
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}

}

PdfiumScanText::PdfiumScanText()
    : m_doc(FPDF_CreateNewDocument())
{
}

PdfiumScanText::~PdfiumScanText()
{
    for (const Page &page : std::as_const(m_pages)) {
        FPDFText_ClosePage(page.text);
        FPDF_ClosePage(page.page);
    }
    if (m_doc) {
        PdfiumFonts::releaseFonts(m_doc);
        FPDF_CloseDocument(m_doc);
    }
}

FPDF_FONT PdfiumScanText::fontFor(const QString &text)
{
    const QList<uint> points = text.toUcs4();
    QString best;
    qsizetype most = -1;
    for (const QString &family : faces()) {
        if (!QFontDatabase::hasFamily(family)) continue;
        auto face = m_faces.find(family);
        if (face == m_faces.end()) face = m_faces.insert(family, QRawFont::fromFont(QFont(family)));
        const qsizetype covered = std::count_if(points.cbegin(), points.cend(), [&](uint p) {
            return p == u' ' || face->supportsCharacter(p);
        });
        if (covered > most) {
            most = covered;
            best = family;
        }
        if (covered == points.size()) break;
    }
    return best.isEmpty() ? nullptr : PdfiumFonts::loadFont(m_doc, best, false, false);
}

void PdfiumScanText::set(int page, const QSizeF &pageSize, const QList<PdfBackend::ScanLine> &lines)
{
    if (!m_doc || m_pages.contains(page)) return;
    Page read;
    read.page = FPDFPage_New(m_doc, FPDF_GetPageCount(m_doc), pageSize.width(), pageSize.height());
    if (!read.page) return;

    // Each line is one invisible text object over its scanned characters, so
    // PDFium finds the same words, spaces and line breaks it finds in text.
    QHash<FPDF_PAGEOBJECT, int> lineOf;
    QList<int> expected(lines.size(), 0);
    for (int l = 0; l < lines.size(); ++l) {
        const PdfBackend::ScanLine &line = lines.at(l);
        QRectF bounds;
        QList<double> heights, tall, small, bottoms;
        const QList<uint> points = line.text.toUcs4();
        for (qsizetype c = 0; c < line.boxes.size(); ++c) {
            const QRectF &box = line.boxes.at(c);
            if (box.isEmpty()) continue;
            bounds = bounds.isNull() ? box : bounds.united(box);
            heights.append(box.height());
            bottoms.append(box.bottom());
            const char32_t ch = c < points.size() ? points.at(c) : 0;
            if ((ch >= u'A' && ch <= u'Z' && ch != u'Q' && ch != u'J') || (ch >= u'0' && ch <= u'9')
                    || QStringLiteral("bdfhklt").contains(QChar(ch)))
                tall.append(box.height());
            else if (QStringLiteral("acemnorsuvwxz").contains(QChar(ch)))
                small.append(box.height());
        }
        FPDF_FONT font = bounds.isEmpty() ? nullptr : fontFor(line.text);
        if (!font) continue;
        // Capitals, digits and ascenders reach about seven tenths of the size,
        // small letters about half; most characters stand on the baseline.
        const double em = tall.size() >= 3 ? median(tall) / 0.71
                        : small.size() >= 3 ? median(small) / 0.52 : median(heights) / 0.7;
        const float size = float(std::clamp(em, 1.0, 1000.0));
        FPDF_PAGEOBJECT object = FPDFPageObj_CreateTextObj(m_doc, font, size);
        const std::u16string text = line.text.toStdU16String();
        FPDFText_SetText(object, reinterpret_cast<FPDF_WIDESTRING>(text.c_str()));
        float left = 0, bottom = 0, right = 0, top = 0;
        FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top);
        const double stretch = right > left ? bounds.width() / (right - left) : 1.0;
        FPDFPageObj_Transform(object, stretch, 0, 0, 1, bounds.left() - left * stretch,
                              pageSize.height() - median(bottoms));
        FPDFTextObj_SetTextRenderMode(object, FPDF_TEXTRENDERMODE_INVISIBLE);
        FPDFPage_InsertObject(read.page, object);
        lineOf.insert(object, l);
        expected[l] = int(line.text.toUcs4().size());
    }
    read.text = FPDFText_LoadPage(read.page);
    if (!read.text) {
        FPDF_ClosePage(read.page);
        return;
    }

    // Each character PDFium reads gets the box OCR found for it; spaces and
    // line breaks PDFium adds have none.
    QList<int> taken(lines.size(), 0);
    const int count = FPDFText_CountChars(read.text);
    for (int i = 0; i < count; ++i) {
        int line = -1;
        QRectF box;
        if (FPDFText_IsGenerated(read.text, i) != 1) {
            line = lineOf.value(FPDFText_GetTextObject(read.text, i), -1);
            if (line >= 0) box = lines.at(line).boxes.value(taken[line]++);
        }
        read.boxes.append(box);
        read.lines.append(line);
    }
    // A space stands between the letters around it, also where OCR let their boxes overlap.
    for (int i = 1; i + 1 < count; ++i) {
        const QRectF &before = read.boxes.at(i - 1), &after = read.boxes.at(i + 1);
        if (!read.boxes.at(i).isEmpty() || read.lines.at(i) < 0 || before.isEmpty() || after.isEmpty()
                || read.lines.at(i - 1) != read.lines.at(i) || read.lines.at(i + 1) != read.lines.at(i))
            continue;
        const double x = std::max(before.left() + 0.01, std::min(before.right(), after.left() - 0.01));
        read.boxes[i] = QRectF(x, before.top(), 0.01, before.height());
    }
    // A line PDFium read with other characters than were set is shown where PDFium placed them.
    for (int i = 0; i < count; ++i) {
        const int line = read.lines.at(i);
        if (line < 0 || taken.at(line) == expected.at(line)) continue;
        double l = 0, r = 0, b = 0, t = 0;
        read.boxes[i] = FPDFText_GetCharBox(read.text, i, &l, &r, &b, &t)
            ? QRectF(l, pageSize.height() - t, r - l, t - b) : QRectF();
    }
    m_pages.insert(page, read);
}

QList<PdfBackend::TextMatch> PdfiumScanText::find(int page, const QString &text) const
{
    QList<PdfBackend::TextMatch> matches;
    const auto it = m_pages.constFind(page);
    if (it == m_pages.cend() || text.isEmpty()) return matches;
    const std::u16string needle = text.toStdU16String();
    FPDF_SCHHANDLE search = FPDFText_FindStart(it->text, reinterpret_cast<FPDF_WIDESTRING>(needle.c_str()), 0, 0);
    while (search && FPDFText_FindNext(search)) {
        const int start = FPDFText_GetSchResultIndex(search);
        const int end = std::min(start + FPDFText_GetSchCount(search), int(it->boxes.size()));
        PdfBackend::TextMatch match;
        match.page = page;
        int line = -1;
        for (int i = std::max(0, start); i < end; ++i) {
            const QRectF &box = it->boxes.at(i);
            if (box.isEmpty()) continue;
            if (match.rects.isEmpty() || it->lines.at(i) != line) match.rects.append(box);
            else match.rects.last() = match.rects.last().united(box);
            line = it->lines.at(i);
        }
        if (!match.rects.isEmpty()) matches.append(match);
    }
    if (search) FPDFText_FindClose(search);
    return matches;
}

#endif
