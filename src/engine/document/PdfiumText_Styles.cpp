#include "engine/document/PdfiumText.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumFonts.hpp"
#include "engine/document/PdfiumPageSpace.hpp"

#include "fpdf_edit.h"
#include "fpdf_text.h"

#include <algorithm>
#include <cmath>

namespace {

double median(QList<double> values)
{
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}

}

namespace PdfiumText {

Typeset::Style Typeset::styleOf(FPDF_PAGEOBJECT obj, const PdfiumPageSpace &space)
{
    Style s;
    s.font = FPDFTextObj_GetFont(obj);
    float size = 0.f;
    FPDFTextObj_GetFontSize(obj, &size);
    FS_MATRIX m { 1, 0, 0, 1, 0, 0 };
    FPDFPageObj_GetMatrix(obj, &m);
    // The matrix as seen on the shown page, so a turned page changes nothing.
    const QPointF across = space.seen(m.a, m.b), up = space.seen(m.c, m.d);
    if (std::abs(across.y()) <= 1e-4 * std::abs(up.y()) && across.x() > 0.0 && up.y() > 0.0) {
        s.size = size * up.y();
        s.a    = across.x() / up.y();
        s.c    = up.x() / up.y();
    } else {
        // Turned or mirrored text is set upright in the box.
        const double scale = std::sqrt(std::abs(double(m.a) * m.d - double(m.b) * m.c));
        s.size = size * (scale > 0.0 ? scale : 1.0);
    }
    unsigned int r = 0, g = 0, b = 0, alpha = 255;
    if (FPDFPageObj_GetFillColor(obj, &r, &g, &b, &alpha)) s.fill = QColor(r, g, b, alpha);
    if (FPDFPageObj_GetStrokeColor(obj, &r, &g, &b, &alpha)) s.stroke = QColor(r, g, b, alpha);
    float width = 0.f;
    if (FPDFPageObj_GetStrokeWidth(obj, &width)) s.strokeWidth = width;
    const int mode = FPDFTextObj_GetTextRenderMode(obj);
    // An OCR layer's text stays invisible where it is set again in place.
    if (mode == FPDF_TEXTRENDERMODE_STROKE || mode == FPDF_TEXTRENDERMODE_FILL_STROKE
            || mode == FPDF_TEXTRENDERMODE_INVISIBLE)
        s.mode = mode;
    return s;
}

bool Typeset::sameStyle(const Style &x, const Style &y)
{
    return x.font == y.font && std::abs(x.size - y.size) < 0.01
        && std::abs(x.a - y.a) < 1e-3 && std::abs(x.c - y.c) < 1e-3 && x.fill == y.fill
        && x.mode == y.mode && (x.mode == 0 || (x.stroke == y.stroke
                                                && std::abs(x.strokeWidth - y.strokeWidth) < 0.01))
        && std::abs(x.track - y.track) < 0.01 && std::abs(x.rise - y.rise) < 0.05;
}

int Typeset::addStyle(const Style &s)
{
    for (int i = 0; i < m_styles.size(); ++i)
        if (sameStyle(m_styles.at(i), s)) {
            m_styles[i].known += s.known;
            return i;
        }
    m_styles.append(s);
    return int(m_styles.size()) - 1;
}

void Typeset::findStyleSources()
{
    // Where a character of each style sits, so a copy of the text can carry it.
    m_styleSources = QList<TextLayout::Source>(m_styles.size());
    const int page = m_edit.sourcePage >= 0 ? m_edit.sourcePage : m_edit.page;
    int at = 0;
    for (const TextLayout::OriginalLine &line : m_edit.originalLines) {
        for (int k = 0; k < line.text.size() && k < line.charX.size(); ++k) {
            const int style = m_originalStyles.value(at + k, -1);
            if (style < 0 || style >= m_styleSources.size() || m_styleSources.at(style).isValid()
                    || line.text.at(k).isSpace())
                continue;
            const Style &s = m_styles.at(style);
            m_styleSources[style] = { page, QPointF(line.charX.at(k) + 0.5,
                                                    line.baseline - s.rise - s.size * 0.3), s.rise };
        }
        at += int(line.text.size()) + 1;
    }
}

void Typeset::addSourceStyles()
{
    // A pasted character takes the style of the character it was copied from.
    for (const TextLayout::Source &source : TextLayout::distinctSources(m_edit.sources)) {
        FPDF_PAGE page = m_page;
        FPDF_TEXTPAGE text = m_textPage;
        if (source.page != m_edit.page) {
            // A page given by the writer is read as it was before any edit.
            page = m_givenPages ? m_givenPages->value(source.page) : nullptr;
            const bool own = !page;
            if (own) page = FPDF_LoadPage(m_doc, source.page);
            text = page ? FPDFText_LoadPage(page) : nullptr;
            if (page) m_sourcePages.emplace_back(own ? page : nullptr, text);
        }
        if (!text) continue;
        const PdfiumPageSpace space(page);
        const QPointF pdf = space.toPdf(source.at);
        const int index = FPDFText_GetCharIndexAtPos(text, pdf.x(), pdf.y(), 2.0, 2.0);
        FPDF_PAGEOBJECT obj = index >= 0 ? FPDFText_GetTextObject(text, index) : nullptr;
        if (!obj) continue;
        Style s = styleOf(obj, space);
        if (s.mode == FPDF_TEXTRENDERMODE_INVISIBLE) {
            // Text copied from a scan's OCR layer is pasted to be seen, unstretched.
            s.mode = 0;
            s.a    = 1.0;
            s.c    = 0.0;
        }
        s.known = objectText(obj, text);
        s.rise  = source.rise;
        const int style = addStyle(s);
        m_sources.append(source);
        m_sourceStyles.append(style);
        if (style >= m_styleSources.size()) m_styleSources.resize(style + 1);
        if (!m_styleSources.at(style).isValid()) m_styleSources[style] = source;
    }
}

void Typeset::applyMarks()
{
    // Each style a marked character has gets a counterpart with the mark on it.
    m_unmarked = m_charStyles;
    if (m_edit.marks.size() != m_edit.newText.size() || m_text.size() != m_edit.newText.size())
        return;
    m_marks = TextLayout::distinctMarks(m_edit.marks);
    bool underlined = false;
    for (const TextLayout::Mark &m : m_edit.marks) underlined |= m.underline.has_value();
    if (m_marks.isEmpty() && !underlined) return;
    m_charMarks = m_edit.marks;
    const int base = int(m_styles.size());
    for (const TextLayout::Mark &mark : std::as_const(m_marks)) {
        EditSession::Edit e = m_edit;
        e.familyChanged = !mark.family.isEmpty();
        e.boldChanged   = mark.bold.has_value();
        e.italicChanged = mark.italic.has_value();
        if (e.familyChanged) e.fontFamily = mark.family;
        if (e.boldChanged)   e.bold = *mark.bold;
        if (e.italicChanged) e.italic = *mark.italic;
        QList<int> styles;
        for (int b = 0; b < base; ++b) {
            Style s = m_styles.at(b);
            if (mark.color.isValid()) s.fill = mark.color;
            restyle(s, e);
            if (s.mode == FPDF_TEXTRENDERMODE_FILL_STROKE && mark.color.isValid()) s.stroke = mark.color;
            if (mark.scale > 0.0) {
                s.size        *= mark.scale;
                s.strokeWidth *= mark.scale;
            }
            if (e.familyChanged) s.family = mark.family;
            styles.append(addStyle(s));
        }
        m_markStyles.append(styles);
    }
    for (qsizetype i = 0; i < m_charStyles.size(); ++i) {
        const qsizetype m = m_charMarks.at(i).styled() ? m_marks.indexOf(m_charMarks.at(i)) : -1;
        if (m >= 0) m_charStyles[i] = m_markStyles.at(m).value(m_charStyles.at(i), m_charStyles.at(i));
    }
    // Text with parts in another look flows anew, as a box restyled whole does.
    if (!m_marks.isEmpty()) m_restyled = true;
}

bool Typeset::underlinedAt(int position) const
{
    const TextLayout::Mark mark = m_charMarks.value(position);
    return mark.underline.value_or(m_edit.underline);
}

void Typeset::readStyles(const QList<FPDF_PAGEOBJECT> &sources, const QList<double> &rise)
{
    const PdfiumPageSpace space(m_page);
    QHash<FPDF_PAGEOBJECT, Style> perObject;
    for (FPDF_PAGEOBJECT obj : sources)
        if (!perObject.contains(obj)) perObject.insert(obj, styleOf(obj, space));

    // Letter spacing is what the writer put between every two glyphs of an
    // object beyond their widths; it is kept for text that flows anew.
    QHash<FPDF_PAGEOBJECT, QList<double>> gaps;
    int at = 0;
    for (const TextLayout::OriginalLine &line : m_edit.originalLines) {
        for (int k = 0; k + 1 < line.text.size(); ++k) {
            FPDF_PAGEOBJECT obj = sources.at(at + k);
            const QChar ch = line.text.at(k), next = line.text.at(k + 1);
            if (obj != sources.at(at + k + 1) || ch.isSpace() || next.isSpace()
                    || ch.isSurrogate() || next.isSurrogate() || k + 1 >= line.charX.size())
                continue;
            const Style &s = perObject[obj];
            if (PdfiumFonts::isType3(s.font)) continue;
            const double natural = PdfiumFonts::glyphWidth(s.font, ch.unicode(), s.size) * s.a;
            gaps[obj].append(line.charX.at(k + 1) - line.charX.at(k) - natural);
        }
        at += int(line.text.size()) + 1;
    }
    for (auto it = gaps.cbegin(); it != gaps.cend(); ++it) {
        const double track = it.value().size() >= 2 ? median(it.value()) : 0.0;
        if (std::abs(track) >= 0.05) perObject[it.key()].track = track;
    }
    QHash<FPDF_PAGEOBJECT, QList<double>> rises;
    for (int i = 0; i < sources.size() && i < rise.size(); ++i)
        if (i < m_original.size() && !m_original.at(i).isSpace())
            rises[sources.at(i)].append(rise.at(i));
    for (auto it = rises.cbegin(); it != rises.cend(); ++it) {
        const double shift = median(it.value());
        if (std::abs(shift) >= 0.3) perObject[it.key()].rise = shift;
    }

    QHash<FPDF_PAGEOBJECT, int> index;
    for (FPDF_PAGEOBJECT obj : sources)
        if (!index.contains(obj)) index.insert(obj, addStyle(perObject[obj]));
    m_originalStyles.reserve(sources.size());
    for (int i = 0; i < sources.size(); ++i) {
        const int style = index.value(sources.at(i));
        m_originalStyles.append(style);
        if (i < m_original.size()) m_styles[style].known += m_original.at(i);
    }
}

}

#endif
