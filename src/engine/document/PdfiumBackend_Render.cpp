#include "engine/document/PdfiumBackend.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumEdits.hpp"
#include "engine/document/PdfiumLock.hpp"
#include "engine/document/PdfiumPageSpace.hpp"
#include "engine/document/PdfiumPart.hpp"
#include "engine/edit/EditSession.hpp"

#include "fpdf_edit.h"
#include "fpdf_progressive.h"

#include <QHash>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

struct RenderPause : IFSDK_PAUSE {
    const std::function<bool()> *cancelled { nullptr };
};

FPDF_BOOL needToPause(IFSDK_PAUSE *self)
{
    const auto *pause = static_cast<RenderPause *>(self);
    return PdfiumLock::othersWaiting() || (*pause->cancelled)();
}

// Renders progressively, giving the lock up whenever another thread waits for it.
bool renderProgressive(FPDF_BITMAP bmp, FPDF_PAGE pg, const QRect &target, const QSize &px,
                       const std::function<bool()> &cancelled)
{
    RenderPause pause;
    pause.version        = 1;
    pause.NeedToPauseNow = &needToPause;
    pause.user           = nullptr;
    pause.cancelled      = &cancelled;
    int status = FPDF_RenderPageBitmap_Start(bmp, pg, -target.x(), -target.y(),
                                             px.width(), px.height(), 0, FPDF_ANNOT, &pause);
    while (status == FPDF_RENDER_TOBECONTINUED && !cancelled()) {
        PdfiumLock::yieldToOthers();
        status = FPDF_RenderPage_Continue(pg, &pause);
    }
    FPDF_RenderPage_Close(pg);
    return status == FPDF_RENDER_DONE;
}

}

QImage PdfiumBackend::renderEditText(int page, qreal scale, const QList<EditSession::Edit> &edits,
                                     const QRect &pixels, bool lifted) const
{
    // Only the text, on transparency, to be laid over the page without it; lifted, with what of a scan it leaves in place.
    PdfiumLock lock;
    if (!m_doc || edits.isEmpty()) return {};
    const QSize px = pixelSize(page, scale);
    const QRect target = pixels.intersected(QRect(QPoint(), px));
    if (target.isEmpty()) return {};
    FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
    if (!pg) return {};
    PdfiumEdits::textAlone(m_doc, pg, page, edits, &m_scanInk, lifted);
    QImage out(target.size(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(target.width(), target.height(), FPDFBitmap_BGRA,
                                          out.bits(), int(out.bytesPerLine()));
    if (bmp) {
        FPDF_RenderPageBitmap(bmp, pg, -target.x(), -target.y(), px.width(), px.height(), 0, 0);
        FPDFBitmap_Destroy(bmp);
    }
    FPDF_ClosePage(pg);
    return out;
}

QImage PdfiumBackend::renderPage(int page, qreal scale) const
{
    return renderPageInternal(page, scale, nullptr, nullptr);
}

QImage PdfiumBackend::renderPage(int page, qreal scale,
                                 const EditSession *session) const
{
    return renderPageInternal(page, scale, session, nullptr);
}

QImage PdfiumBackend::renderPageInBackground(int page, qreal scale,
                                             const EditSession *session,
                                             const std::function<bool()> &cancelled,
                                             const QRect &area, quint64 state) const
{
    return renderPageInternal(page, scale, session, &cancelled, area, state);
}

QImage PdfiumBackend::renderPageInternal(int page, qreal scale,
                                         const EditSession *session,
                                         const std::function<bool()> *cancelled,
                                         const QRect &area, quint64 state) const
{
    PdfiumLock lock;
    if (!m_doc) return {};
    const QSize px = pixelSize(page, scale);
    if (px.isEmpty()) return {};
    const QRect target = area.isEmpty() ? QRect(QPoint(), px)
                                        : area.intersected(QRect(QPoint(), px));
    if (target.isEmpty()) return {};

    // Form fields are painted over the whole page after rendering.
    if (session && target.size() != px) {
        for (const EditSession::Edit &edit : session->snapshotEdits()) {
            if (edit.page != page || edit.formField.isEmpty()) continue;
            const QImage full = renderPageInternal(page, scale, session, cancelled, {}, state);
            return full.isNull() ? full : full.copy(target);
        }
    }

    if (session && session->hasFormChangesOnPage(page)) {
        QImage filled = renderFormPage(page, px, target, *session);
        if (!filled.isNull() && target.size() == px) session->paintFormFields(page, filled, scale);
        return filled;
    }

    const EditSession *notes = session;
    if (session && !session->hasEditsOnPage(page)
            && !session->hasImageEditsOnPage(page)
            && !session->hasDrawEditsOnPage(page)
            && !session->hasLinkEditsOnPage(page)
            && !session->hasRemovedHighlightsOnPage(page))
        session = nullptr;

    // Background renders keep the page loaded, so the tiles of one page share
    // its parsed content and decoded images; an edited page is kept with the
    // edits applied for as long as its state stays the same.
    const bool keep = cancelled && (!session || state != 0);
    FPDF_PAGE pg = keep ? backgroundPage(page, session ? state : 0, session)
                        : FPDF_LoadPage(m_doc, page);
    if (!pg) return {};
    const auto closePage = [this, keep, pg] { if (!keep) closeShownPage(pg); };

    if (session && !keep) PdfiumEdits::applyToPage(m_doc, pg, page, *session, nullptr, &m_scanInk);
    PdfiumEdits::showNoteHighlights(pg, page, notes);

    const PdfiumPart::Frame frame = PdfiumPart::frameFor(pg, target, px);
    FPDF_BITMAP bmp = FPDFBitmap_Create(frame.area.width(), frame.area.height(), 0);
    if (!bmp) { closePage(); return {}; }
    FPDFBitmap_FillRect(bmp, 0, 0, frame.area.width(), frame.area.height(), 0xFFFFFFFF);
    if (cancelled) {
        if (!renderProgressive(bmp, pg, frame.area, px, *cancelled)) {
            FPDFBitmap_Destroy(bmp);
            closePage();
            return {};
        }
    } else {
        FPDF_RenderPageBitmap(bmp, pg, frame.startX(), frame.startY(), px.width(), px.height(),
                              0, FPDF_ANNOT);
    }
    drawForms(bmp, pg, frame.area, px);

    QImage rendered = PdfiumPart::cut(bmp, frame, target.size());
    FPDFBitmap_Destroy(bmp);
    closePage();

    if (session && !rendered.isNull() && target.size() == px)
        session->paintFormFields(page, rendered, scale);

    return rendered;
}

namespace {

QHash<FPDF_PAGEOBJECT, QRectF> objectBounds(FPDF_PAGE page)
{
    const PdfiumPageSpace space(page);
    QHash<FPDF_PAGEOBJECT, QRectF> out;
    for (int i = 0, n = FPDFPage_CountObjects(page); i < n; ++i) {
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, i);
        float left = 0, bottom = 0, right = 0, top = 0;
        if (!object || !FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top)) continue;
        out.insert(object, space.toView(left, bottom, right, top));
    }
    return out;
}

QRectF differingBounds(const QHash<FPDF_PAGEOBJECT, QRectF> &before,
                       const QHash<FPDF_PAGEOBJECT, QRectF> &after)
{
    QRectF changed;
    const auto collect = [&changed](const QHash<FPDF_PAGEOBJECT, QRectF> &from,
                                    const QHash<FPDF_PAGEOBJECT, QRectF> &other) {
        for (auto it = from.cbegin(); it != from.cend(); ++it) {
            const auto match = other.constFind(it.key());
            if (match != other.cend() && *match == it.value()) continue;
            changed = changed.isNull() ? it.value() : changed.united(it.value());
        }
    };
    collect(before, after);
    collect(after, before);
    return changed;
}

}

FPDF_PAGE PdfiumBackend::backgroundPage(int page, quint64 state,
                                        const EditSession *session) const
{
    constexpr size_t kKept = 3;
    for (size_t i = 0; i < m_backgroundPages.size(); ++i) {
        if (m_backgroundPages[i].index != page || m_backgroundPages[i].state != state) continue;
        const BackgroundPage hit = m_backgroundPages[i];
        m_backgroundPages.erase(m_backgroundPages.begin() + qsizetype(i));
        m_backgroundPages.insert(m_backgroundPages.begin(), hit);
        return hit.page;
    }
    FPDF_PAGE loaded = FPDF_LoadPage(m_doc, page);
    if (!loaded) return nullptr;
    if (state && session) PdfiumEdits::applyToPage(m_doc, loaded, page, *session, nullptr, &m_scanInk);
    m_backgroundPages.insert(m_backgroundPages.begin(), { page, state, loaded });
    while (m_backgroundPages.size() > kKept) {
        closeShownPage(m_backgroundPages.back().page);
        m_backgroundPages.pop_back();
    }
    return loaded;
}

void PdfiumBackend::dropBackgroundPages() const
{
    for (const BackgroundPage &kept : m_backgroundPages) closeShownPage(kept.page);
    m_backgroundPages.clear();
}

PdfBackend::AreaRender PdfiumBackend::renderChanges(int page, qreal scale,
                                                    const EditSession *session,
                                                    const QRect &include,
                                                    bool wholePage) const
{
    PdfiumLock lock;
    AreaRender result;
    const QSize px = pixelSize(page, scale);
    if (!m_doc || px.isEmpty()) return result;
    const EditSession *notes = session;
    if (session && !session->hasEditsOnPage(page) && !session->hasImageEditsOnPage(page)
            && !session->hasDrawEditsOnPage(page) && !session->hasLinkEditsOnPage(page)
            && !session->hasRemovedHighlightsOnPage(page) && !session->hasFormChangesOnPage(page))
        session = nullptr;
    const QRect pageRect(QPoint(), px);

    // Form fields are painted over the render or filled in, link edits and removed
    // highlights change annotations; none shows up as a changed page object.
    bool painted = false;
    if (session) {
        painted = session->hasLinkEditsOnPage(page) || session->hasRemovedHighlightsOnPage(page)
               || session->hasFormChangesOnPage(page);
        for (const EditSession::Edit &edit : session->snapshotEdits())
            if (edit.page == page && !edit.formField.isEmpty()) painted = true;
    }
    if (painted) {
        result.image   = renderPageInternal(page, scale, session, nullptr);
        result.pixels  = pageRect;
        result.changed = pageRect;
        return result;
    }

    // The unedited page is only read, so a kept one serves.
    FPDF_PAGE pg = session ? FPDF_LoadPage(m_doc, page) : backgroundPage(page);
    const auto closePage = [this, session, pg] { if (session) closeShownPage(pg); };
    if (!pg) return result;

    if (session) {
        const QSizeF pts = pageSizePts(page);
        const QHash<FPDF_PAGEOBJECT, QRectF> before = objectBounds(pg);
        PdfiumEdits::applyToPage(m_doc, pg, page, *session, nullptr, &m_scanInk);
        QRectF changedPts = differingBounds(before, objectBounds(pg));
        for (const EditSession::Edit &edit : session->snapshotEdits()) {
            if (edit.page != page) continue;
            for (const QRectF &rect : edit.eraseRects) changedPts = changedPts.united(rect);
            changedPts = changedPts.united(EditSession::shownBounds(edit));
        }
        if (!changedPts.isEmpty() && !pts.isEmpty()) {
            const qreal sx = px.width() / pts.width(), sy = px.height() / pts.height();
            result.changed = QRect(QPoint(qFloor(changedPts.left() * sx) - 2,
                                          qFloor(changedPts.top() * sy) - 2),
                                   QPoint(qCeil(changedPts.right() * sx) + 2,
                                          qCeil(changedPts.bottom() * sy) + 2))
                                 .intersected(pageRect);
        }
    }

    const QRect target = wholePage ? pageRect : result.changed.united(include).intersected(pageRect);
    if (target.isEmpty()) {
        closePage();
        return result;
    }

    PdfiumEdits::showNoteHighlights(pg, page, notes);
    const PdfiumPart::Frame frame = PdfiumPart::frameFor(pg, target, px);
    FPDF_BITMAP bmp = FPDFBitmap_Create(frame.area.width(), frame.area.height(), 0);
    if (!bmp) { closePage(); return result; }
    FPDFBitmap_FillRect(bmp, 0, 0, frame.area.width(), frame.area.height(), 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bmp, pg, frame.startX(), frame.startY(), px.width(), px.height(),
                          0, FPDF_ANNOT);
    drawForms(bmp, pg, frame.area, px);
    result.image = PdfiumPart::cut(bmp, frame, target.size());
    FPDFBitmap_Destroy(bmp);
    closePage();
    result.pixels = target;
    return result;
}

#endif
