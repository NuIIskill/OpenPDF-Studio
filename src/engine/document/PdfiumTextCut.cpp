#include "engine/document/PdfiumTextCut.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumFonts.hpp"
#include "engine/document/PdfiumPageSpace.hpp"
#include "engine/document/PdfiumText.hpp"
#include "engine/document/PdfiumTextRules.hpp"
#include "engine/edit/EditSession.hpp"

#include "fpdf_edit.h"
#include "fpdf_text.h"

#include <QString>

#include <algorithm>
#include <cmath>

namespace {

bool isInside(const QList<QRectF> &areas, const QRectF &box, const QPointF &origin,
              PdfiumTextCut::Inside inside)
{
    for (const QRectF &area : areas) {
        if (inside == PdfiumTextCut::Inside::Centre) {
            if (area.contains(box.center())) return true;
        } else if (box.isEmpty() ? area.contains(origin) : area.intersects(box)) {
            return true;
        }
    }
    return false;
}

}

// The characters of text objects only partly inside the areas that lie
// outside them, each run as a line of its own, so they can be set again where
// they were: a frame may take part of an object.
PdfiumTextCut::Result PdfiumTextCut::cut(FPDF_TEXTPAGE textPage, const QList<QRectF> &areas,
                                         const PdfiumPageSpace &space, Inside inside)
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
        byObject[obj].append({ text, box, origin, isInside(areas, box, origin, inside),
                               !text.trimmed().isEmpty() && !box.isEmpty() });
    }

    Result out;
    out.seen = QSet<FPDF_PAGEOBJECT>(order.cbegin(), order.cend());
    for (FPDF_PAGEOBJECT obj : std::as_const(order)) {
        const QList<Char> &chars = byObject[obj];
        // A space inside is taken too, but only ink decides whether all is:
        // a line's area ends before the spaces that end it.
        const bool any = std::any_of(chars.cbegin(), chars.cend(),
                                     [](const Char &c) { return c.inside; });
        const bool all = std::all_of(chars.cbegin(), chars.cend(),
                                     [](const Char &c) { return !c.ink || c.inside; });
        if (!any) continue;
        if (all) { out.whole.append(obj); continue; }
        if (turned.contains(obj)) { out.turned.append(obj); continue; }
        out.cut.append(obj);
        TextLayout::OriginalLine run;
        const auto flush = [&]() {
            if (!run.text.trimmed().isEmpty()) out.rest.append(run);
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

std::shared_ptr<PdfiumText::Typeset> PdfiumTextCut::setAgain(FPDF_DOCUMENT doc, FPDF_PAGE page,
                                                             FPDF_TEXTPAGE textPage, int pageIndex,
                                                             const TextLayout::OriginalLine &line,
                                                             const QHash<int, FPDF_PAGE> *sourcePages)
{
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
    return std::make_shared<PdfiumText::Typeset>(doc, page, textPage, rest, nullptr, true,
                                                 sourcePages, false);
}

#endif
