#include "ui/edit/EditController.hpp"

#include "ui/view/PageCanvas.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentSource.hpp"
#  include "engine/document/PdfBackend.hpp"
#  include "engine/render/PdfRenderer.hpp"
#  include "engine/edit/ContentMap.hpp"
#  include "engine/edit/ContentModel.hpp"
#  include "engine/edit/InkMetrics.hpp"
#  include "ui/edit/TextBoxFrame.hpp"
#  include "ui/view/HoverHighlight.hpp"
#  include "ui/view/ZoomController.hpp"

#  include <QApplication>
#  include <QFontMetricsF>
#  include <QLabel>
#  include <QStringList>
#  include <QUndoStack>
#  include <QWidget>
#  include <algorithm>
#endif

// Undo/redo command for text edits: stores before/after EditSession snapshots.
// QUndoStack::push() calls redo() immediately, so we skip the first redo() call
// since the edit is already committed at push-time.
class EditUndoCmd : public QUndoCommand
{
    EditSession              *m_session;
    EditController           *m_ctl;
    int                       m_srcPage, m_dstPage;  // differ when the box was dragged onto another page
    QList<EditSession::Edit>  m_before, m_after;
    bool                      m_firstRedo { true };
public:
    EditUndoCmd(EditSession *s, EditController *v, int srcPage, int dstPage,
                QList<EditSession::Edit> before,
                QList<EditSession::Edit> after)
        : QUndoCommand(EditController::tr("Text bearbeiten"))
        , m_session(s), m_ctl(v), m_srcPage(srcPage), m_dstPage(dstPage)
        , m_before(std::move(before)), m_after(std::move(after)) {}

    void undo() override {
        m_session->restoreEdits(m_before);
        rerender();
    }
    void redo() override {
        if (m_firstRedo) { m_firstRedo = false; return; }
        m_session->restoreEdits(m_after);
        rerender();
    }
private:
    void rerender() {
        Q_EMIT m_ctl->pageNeedsRerender(m_srcPage);
        if (m_dstPage != m_srcPage)
            Q_EMIT m_ctl->pageNeedsRerender(m_dstPage);
    }
};

void EditController::refreshFontLive()
{
#ifdef HAVE_PDF_RENDERING
    if (activeEditPage < 0 || !m_frame->isVisible()) return;
    m_frame->setTextFont(currentEditorFontFamily,
                               currentEditorBold, currentEditorItalic);
#endif
}

void EditController::setFontFamily(const QString &family)
{
#ifdef HAVE_PDF_RENDERING
    if (family.isEmpty() || family == currentEditorFontFamily) return;
    currentEditorFontFamily = family;
    editorFontChangedByUser = true;
    refreshFontLive();
#else
    Q_UNUSED(family)
#endif
}

void EditController::setBold(bool on)
{
#ifdef HAVE_PDF_RENDERING
    if (on == currentEditorBold) return;
    currentEditorBold       = on;
    editorFontChangedByUser = true;
    refreshFontLive();
#else
    Q_UNUSED(on)
#endif
}

void EditController::setItalic(bool on)
{
#ifdef HAVE_PDF_RENDERING
    if (on == currentEditorItalic) return;
    currentEditorItalic     = on;
    editorFontChangedByUser = true;
    refreshFontLive();
#else
    Q_UNUSED(on)
#endif
}

void EditController::setFontSize(int ptSize)
{
#ifdef HAVE_PDF_RENDERING
    if (ptSize < 4 || ptSize > 400) return;
    if (activeEditPage >= 0 && m_frame->isVisible()) {
        const qreal scale = PdfRenderer::screenScale(m_zoom->zoom());
        // Scale the edit bounds proportionally so line-wrapping is preserved.
        // Both width and height scale with the font ratio so character counts
        // per line stay the same and formatting doesn't change.
        if (currentEditorRenderSizePt > 0 && ptSize != currentEditorRenderSizePt) {
            const qreal ratio = (qreal)ptSize / currentEditorRenderSizePt;
            const QPointF anchor = activeEditBounds.topLeft();
            activeEditBounds = QRectF(anchor, activeEditBounds.size() * ratio);
            clampToPdfPage(activeEditPage, activeEditBounds);
        }
        // A size the user typed is meant literally, in the document's own
        // font — no calibration to a substitute face on top of it.
        currentEditorFontSizePt   = ptSize;
        currentEditorRenderSizePt = ptSize;
        editorSizeChangedByUser   = true;
        const QLabel *lbl = m_canvas->pageLabel(activeEditPage);
        if (lbl) {
            m_frame->setPageRect(lbl->geometry());
            const QRectF cb(
                activeEditBounds.topLeft() * scale + QPointF(lbl->pos()),
                activeEditBounds.size() * scale);
            m_frame->repositionForZoom(cb, qMax(6, qRound(ptSize * scale)));
        }
    } else {
        currentEditorFontSizePt   = ptSize;
        currentEditorRenderSizePt = ptSize;
        editorSizeChangedByUser   = true;
    }
#else
    Q_UNUSED(ptSize)
#endif
}

void EditController::setTextColor(const QColor &color)
{
#ifdef HAVE_PDF_RENDERING
    if (!color.isValid()) return;
    currentEditorColor = color;
    if (activeEditPage >= 0 && m_frame->isVisible())
        m_frame->setTextColor(color);
#else
    Q_UNUSED(color)
#endif
}

TextBoxProperties EditController::textBoxProperties() const
{
    TextBoxProperties p = currentBox;
    p.bounds = activeEditBounds;
    return p;
}

void EditController::notifyBoundsChanged()
{
    if (activeEditPage >= 0)
        Q_EMIT textBoxPropertiesChanged(textBoxProperties());
}

void EditController::setTextBoxProperties(const TextBoxProperties &properties)
{
#ifdef HAVE_PDF_RENDERING
    if (activeEditPage < 0 || !m_frame->isVisible()) {
        // The inspector is deliberately usable before a text box exists.
        // Geometry comes from the subsequent drag; every other value becomes
        // the starting style of that new box.
        const QRectF defaultBounds = defaultBox.bounds;
        defaultBox = properties;
        defaultBox.bounds = defaultBounds;
        return;
    }
    currentBox = properties;
    activeEditBounds = properties.bounds.normalized();
    if (activeEditBounds.width() < 1.0) activeEditBounds.setWidth(1.0);
    if (activeEditBounds.height() < 1.0) activeEditBounds.setHeight(1.0);
    clampToPdfPage(activeEditPage, activeEditBounds);
    currentBox.bounds = activeEditBounds;

    const QLabel *lbl = m_canvas->pageLabel(activeEditPage);
    if (!lbl) return;
    const qreal scale = PdfRenderer::screenScale(m_zoom->zoom());
    m_frame->setBoxProperties(currentBox, scale);
    m_frame->setPageRect(lbl->geometry());
    const QRectF cb(activeEditBounds.topLeft() * scale + QPointF(lbl->pos()),
                    activeEditBounds.size() * scale);
    m_frame->repositionForZoom(
        cb, qMax(6, qRound(currentEditorRenderSizePt * scale)));
    Q_EMIT textBoxPropertiesChanged(textBoxProperties());
#else
    Q_UNUSED(properties)
#endif
}

void EditController::setHorizontalAlignment(Qt::Alignment alignment)
{
    TextBoxProperties p = activeEditPage >= 0 ? textBoxProperties() : defaultBox;
    if (alignment & Qt::AlignJustify) p.horizontalAlign = TextBoxProperties::HorizontalAlign::Justify;
    else if (alignment & Qt::AlignHCenter) p.horizontalAlign = TextBoxProperties::HorizontalAlign::Center;
    else if (alignment & Qt::AlignRight) p.horizontalAlign = TextBoxProperties::HorizontalAlign::Right;
    else p.horizontalAlign = TextBoxProperties::HorizontalAlign::Left;
    setTextBoxProperties(p);
}

void EditController::setListStyle(TextBoxProperties::ListStyle style)
{
    TextBoxProperties p = activeEditPage >= 0 ? textBoxProperties() : defaultBox;
    p.listStyle = style;
    setTextBoxProperties(p);
}

void EditController::changeIndent(int delta)
{
    TextBoxProperties p = activeEditPage >= 0 ? textBoxProperties() : defaultBox;
    p.indentLevel = qBound(0, p.indentLevel + delta, 8);
    setTextBoxProperties(p);
}

void EditController::setLineSpacing(double multiplier)
{
    TextBoxProperties p = activeEditPage >= 0 ? textBoxProperties() : defaultBox;
    p.lineSpacingMultiplier = qBound(0.5, multiplier, 4.0);
    activeEditLineSpacingPt = currentEditorFontSizePt * p.lineSpacingMultiplier;
    setTextBoxProperties(p);
}

void EditController::clampToPdfPage(int page, QRectF &r) const
{
#ifdef HAVE_PDF_RENDERING
    if (!m_src->renderer() || page < 0) return;
    const QSizeF ps = m_src->renderer()->pageSizePts(page);
    // Shrink if larger than page
    if (r.width()  > ps.width())  r.setWidth(ps.width());
    if (r.height() > ps.height()) r.setHeight(ps.height());
    // Push inside page boundaries
    if (r.left()   < 0)           r.moveLeft(0);
    if (r.top()    < 0)           r.moveTop(0);
    if (r.right()  > ps.width())  r.moveRight(ps.width());
    if (r.bottom() > ps.height()) r.moveBottom(ps.height());
#else
    Q_UNUSED(page) Q_UNUSED(r)
#endif
}

#ifdef HAVE_PDF_RENDERING

// Page rendered at 3 px per pt — shared by the font-size calibration and the
// color samplers, rendered lazily and at most once. Low-dpi renders consist
// mostly of antialiasing pixels and make both unreliable.
static constexpr qreal kSampleScale = 3.0;

struct EditController::EditOpen
{
    // ── Where the click landed ────────────────────────────────────────────────
    int      page  { -1 };
    QLabel  *label { nullptr };
    QPointF  pdfPt;
    QSizeF   pageSize;
    qreal    scale { 1.0 };
    QList<QRectF> erasedZones;

    // ── What is being edited ──────────────────────────────────────────────────
    bool              isSessionEdit { false };
    EditSession::Edit sessionEdit;
    TextBlock         block;
    ContentItem       contentItem;
    QString           displayText;
    // Bounds came from a whole paragraph, so the single-line height cap in
    // fitEditHeight() must not apply.
    bool              paragraphBounds { false };

    // ── Ink calibration, carried from the font choice to the anchor ───────────
    bool                measurable { false };
    InkMetrics::FontInk probeInk;
    QPointF             textOrigin;

    PdfRenderer *renderer { nullptr };

    const QImage &sampleImage()
    {
        if (m_samp.isNull()) m_samp = renderer->renderPage(block.page, kSampleScale);
        return m_samp;
    }

private:
    QImage m_samp;
};



bool EditController::resolveEditTarget(const QPoint &canvasPos, EditOpen &o)
{
    auto [pageIdx, pageLbl] = m_canvas->pageAtCanvasPos(canvasPos);
    if (pageIdx < 0) return false;

    // Convert canvas position to PDF-point coordinates
    o.page     = pageIdx;
    o.label    = pageLbl;
    o.scale    = PdfRenderer::screenScale(m_zoom->zoom());
    o.pdfPt    = QPointF(canvasPos - pageLbl->pos()) / o.scale;
    o.pageSize = m_src->renderer()->pageSizePts(pageIdx);
    o.renderer = m_src->renderer();

    // Session edits take priority over native PDF text.  If the user placed a
    // text box (createTextFrame) or edited text in-place (handleEditClick), their
    // session content is shown on top and clicking that area edits the session
    // content, not the original PDF text underneath.
    // Session-erased areas: text lookup must treat them as gone, or a click
    // near them resurrects the invisible original as a duplicate.
    o.erasedZones = m_session->blankRegions(pageIdx);

    if (m_session->findEditAt(pageIdx, o.pdfPt, &o.sessionEdit)) {
        o.block         = TextBlock{ pageIdx, o.sessionEdit.pdfBounds,
                                     o.sessionEdit.newText };
        o.isSessionEdit = true;
    }

    // Region model for the clicked page (cached): exact bounds, font, colors,
    // paragraph/table structure, and fillable form fields.
    if (!o.isSessionEdit && m_src->contentProvider())
        o.contentItem = m_src->contentProvider()->itemAt(pageIdx, o.pdfPt);

    // Clicking an image/media region with the text tool must not snap to text
    // up to 40 pt away — swallow the click instead (matches the hover UI).
    // Exception: a near-full-page image is a scanned page; those fall through
    // so the OCR path below can offer text editing.
    if (!o.isSessionEdit && !o.contentItem.isValid() && m_src->contentProvider()) {
        const ContentItem nonText = m_src->contentProvider()->itemAt(
            pageIdx, o.pdfPt,
            contentTypeBit(ContentItem::Type::Image)
                | contentTypeBit(ContentItem::Type::Media),
            0.0);
        if (nonText.isValid()) {
            const double pageArea = o.pageSize.width() * o.pageSize.height();
            const double itemArea = nonText.bounds.width()
                                  * nonText.bounds.height();
            if (pageArea > 0.0 && itemArea / pageArea < 0.8)
                return false;
        }
    }

    // Fillable form field: open the editor on the field bounds even when the
    // field is empty — there is no text for the extractor to find.
    if (!o.isSessionEdit && o.contentItem.isFormField())
        o.block = TextBlock{ pageIdx, o.contentItem.bounds, o.contentItem.text };

    // Fall back to native PDF text (vector PDFs)
    if (!o.block.isValid()) {
        if (auto *backend = m_src->backend())
            o.block = backend->textAt(pageIdx, o.pdfPt, o.erasedZones);
    }

    // If still nothing, fall back to OCR (scanned / image PDF)
    if (!o.block.isValid() && m_ocr && m_ocr->isReady()) {
        if (!m_ocrCache.contains(pageIdx)) {
            const qreal ocrScale = qMax(o.scale * m_canvas->canvasWidget()->devicePixelRatioF(), 300.0 / 72.0);
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            const QImage pageImg = m_src->renderer()->renderPage(pageIdx, ocrScale);
            m_ocrCache[pageIdx] = m_ocr->recognizePage(pageImg, o.pageSize,
                                                             ocrScale);
            QApplication::restoreOverrideCursor();
        }
        const OcrEngine::Block ocr = OcrEngine::blockAt(m_ocrCache[pageIdx], o.pdfPt);
        if (ocr.isValid())
            o.block = TextBlock{ pageIdx, ocr.pdfBounds, ocr.text };
    }

    // Last resort: content-stream text of the detected region. Encoding can be
    // imperfect for exotic embedded fonts, but it beats finding nothing.
    if (!o.block.isValid() && o.contentItem.isValid() && o.contentItem.isTextual()
            && !o.contentItem.text.isEmpty())
        o.block = TextBlock{ pageIdx, o.contentItem.bounds, o.contentItem.text };

    if (!o.block.isValid()) return false;

    // If this click was the release of a press that committed an edit on this
    // same block, don't re-open — that would call rerenderPageWithBlank and
    // erase the text we just committed.
    if (o.block.page == lastCommittedPage &&
        o.block.pdfBounds.intersects(lastCommittedOrigBounds)) {
        lastCommittedPage = -1;
        return false;
    }
    lastCommittedPage = -1;

    // If the click lands inside a blank (erase-only) session edit — the source
    // area of a drag-move — don't open any editor.  The area is intentionally
    // empty; pre-filling with the (still present, only visually erased) native
    // PDF text would duplicate it: native text back at P1 alongside the moved
    // text at P2.  The user can drag-to-create a new text frame here instead.
    // Two guards: the click point itself, AND the found block — text lookup is
    // fuzzy (nearest within 40 pt), so a click NEAR the blanked area can snap
    // onto the invisible original even though the point lies outside the blank.
    // NEVER swallow session-edit clicks: moved text often still overlaps its
    // own blank, and its visible content must stay editable on top of it.
    if (!o.isSessionEdit && (m_session->isBlankAt(o.block.page, o.pdfPt)
                             || m_session->isBlankCovering(o.block.page,
                                                           o.block.pdfBounds)))
        return false;

    o.displayText = o.block.text;
    return true;
}

void EditController::applyEditTargetBounds(EditOpen &o)
{
    activeEditPage           = o.block.page;
    activeEditSourcePage     = o.block.page;
    activeEditBounds         = o.block.pdfBounds;
    clampToPdfPage(o.block.page, activeEditBounds);
    activeEditOriginalBounds = activeEditBounds;
    activeEditNeedsBlank     = true;   // editing existing text → must erase original
    currentBox = o.isSessionEdit ? o.sessionEdit.box : TextBoxProperties{};
    activeEditFieldName.clear();

    // Apply the region model: bounds, paragraph text, form-field mode.
    // Bounds priority: glyph-accurate extractor rects win over the region
    // model's estimated character widths (estimates overpaint surroundings);
    // the region model takes over when the extractor found nothing or
    // returned a suspicious multi-row rect.
    if (!o.isSessionEdit && o.contentItem.isValid()) {
        if (!o.contentItem.bounds.isEmpty()) {
            const double fs = o.contentItem.fontSizePt > 0.0
                                  ? o.contentItem.fontSizePt : 12.0;
            const bool extractorUsable = o.block.pdfBounds.height() > 0.5
                                      && o.block.pdfBounds.height() <= fs * 2.5;
            if (!extractorUsable || o.contentItem.isFormField()) {
                activeEditBounds         = o.contentItem.bounds;
                activeEditOriginalBounds = o.contentItem.bounds;
                clampToPdfPage(o.block.page, activeEditBounds);
            }
        }
        if (o.contentItem.type == ContentItem::Type::Paragraph) {
            // The extractor block covers only the clicked line; fetch the full
            // multi-line paragraph (text + glyph-accurate bounds) so it is
            // edited as one unit without overpainting its surroundings.
            o.paragraphBounds = true;
            TextBlock para;
            if (auto *backend = m_src->backend())
                para = backend->blockInRect(o.block.page, o.contentItem.bounds,
                                            o.erasedZones);
            // Sanity: a "paragraph" spanning most of the page is a detection
            // failure — editing it would open a viewport-sized frame. Fall
            // back to the clicked line in that case.
            const bool paraSane = para.isValid()
                               && para.pdfBounds.height() < o.pageSize.height() * 0.5;
            if (paraSane && !para.text.isEmpty()) {
                o.displayText              = para.text;
                activeEditBounds         = para.pdfBounds;
                activeEditOriginalBounds = para.pdfBounds;
                clampToPdfPage(o.block.page, activeEditBounds);
            } else if (!para.isValid() && !o.contentItem.text.isEmpty()) {
                o.displayText = o.contentItem.text;
            } else {
                o.paragraphBounds = false;   // keep the tight line bounds
            }
        }
        if (o.contentItem.isFormField()) {
            activeEditFieldName = o.contentItem.fieldName;
            o.displayText         = o.contentItem.text;
            // The widget appearance renders the value — erase it in the live
            // view only when there is an existing value to hide.
            activeEditNeedsBlank = !o.contentItem.text.isEmpty();
        }
    }
    activeEditOriginalText = o.displayText;
    activeEditPdfText      = o.isSessionEdit ? o.sessionEdit.originalText
                                               : o.displayText;
    if (o.isSessionEdit)
        activeEditFieldName = o.sessionEdit.formField;
}

void EditController::chooseEditorFont(EditOpen &o)
{
    // Font size: stored session value > region-model detection > line-height
    // estimate from the block bounds.
    // sizeIsExact tracks whether the result is the size the PDF itself states.
    // Only the qpdf scanner can know it, and only then may it be written back
    // to the file unchanged — everything else is an estimate that has to be
    // calibrated against the rendered ink further down.
    bool sizeIsExact = false;
    editorSizeChangedByUser = false;
    if (o.isSessionEdit && o.sessionEdit.fontSizePt > 0.0) {
        currentEditorFontSizePt = qMax(4.0, o.sessionEdit.fontSizePt);
        editorSizeChangedByUser = o.sessionEdit.sizeChanged;
        sizeIsExact = true;               // already settled when it was created
    } else {
        const double detectedPt = (o.contentItem.isValid()
                                   && o.contentItem.fontSizePt > 0.0)
                                      ? o.contentItem.fontSizePt : 0.0;
        if (detectedPt > 0.0 && detectedPt <= 144.0) {
            const double polyEst   = o.block.pdfBounds.height() / 0.72;
            const bool   plausible = (detectedPt <= polyEst * 4.0)
                                   || (o.block.pdfBounds.height() >= 20.0);
            if (plausible) {
                // Kept fractional: rounding a 16.5 pt heading down to 16 is a
                // visible shrink, and it is the number written back to the file.
                currentEditorFontSizePt = qMax(4.0, detectedPt);
                sizeIsExact = o.contentItem.fontSizeExact;
            } else {
                currentEditorFontSizePt = qMax(4.0, qMin(polyEst, 28.0));
            }
        } else {
            const int    lineCount = qMax(1, o.displayText.count(u'\n') + 1);
            const double lineH     = qMin(activeEditBounds.height() / lineCount,
                                          20.0);
            currentEditorFontSizePt = qMax(4.0, lineH / 0.72);
        }
    }
    // Sanity: the clicked line's glyph height caps the font size — a
    // detection outlier must not commit 2-3x oversized text.
    if (!o.isSessionEdit && !o.contentItem.isFormField()) {
        const double lineGlyphH = o.block.pdfBounds.height();
        if (lineGlyphH > 4.0 && lineGlyphH < 60.0
                && currentEditorFontSizePt > lineGlyphH * 1.5) {
            currentEditorFontSizePt = qMax(4.0, lineGlyphH * 1.05);
            sizeIsExact = false;          // overridden → back to an estimate
        }
    }

    // Font family/style: stored session value > region-model detection.
    if (o.isSessionEdit) {
        currentEditorFontFamily = o.sessionEdit.fontFamily;
        currentEditorBold       = o.sessionEdit.bold;
        currentEditorItalic     = o.sessionEdit.italic;
        editorFontChangedByUser = o.sessionEdit.fontChanged;
    } else if (!o.contentItem.fontFamily.isEmpty()) {
        currentEditorFontFamily = o.contentItem.fontFamily;
        currentEditorBold       = o.contentItem.bold;
        currentEditorItalic     = o.contentItem.italic;
        editorFontChangedByUser = false;
    } else {
        currentEditorFontFamily.clear();
        currentEditorBold       = false;
        currentEditorItalic     = false;
        editorFontChangedByUser = false;
    }

    // Calibrate against the ORIGINAL ink (see InkMetrics). What must be
    // preserved is how tall the text LOOKS, and the point size alone does not
    // decide that: the editor paints with a different family than the document
    // (always so on the Poppler backend, which reports none) and equal point
    // sizes render visibly different ink there. Editing a line must never
    // resize it, so the size we PAINT with is the one whose ink height matches
    // what the page actually shows.
    //
    // That fitted size is not the one the file gets. An exact size is what the
    // PDF itself states, and the vector save must write it back untouched — a
    // size bent to fit a substitute face would be wrong in the document's own
    // font. So the two part ways here: fitted on screen, exact on disk. Only an
    // estimated size, which is nothing but a guess, is replaced outright.
    currentEditorRenderSizePt = currentEditorFontSizePt;
    o.measurable = !o.isSessionEdit && !o.contentItem.isFormField()
                && !o.displayText.isEmpty() && !o.sampleImage().isNull();
    if (!o.measurable) return;

    const InkMetrics::MeasuredInk origInk =
        InkMetrics::measuredInkPt(o.sampleImage(), activeEditBounds, kSampleScale);
    QFont probe(currentEditorFontFamily.isEmpty()
                    ? QStringLiteral("Helvetica") : currentEditorFontFamily);
    probe.setStyleHint(QFont::SansSerif);
    probe.setBold(currentEditorBold);
    probe.setItalic(currentEditorItalic);
    o.probeInk = InkMetrics::fontInkPerPt(o.displayText, probe);
    if (origInk.height > 1.0 && o.probeInk.heightPerPt > 0.05) {
        // The measurement corrects the estimate, it does not replace it:
        // ink that can't be told apart from its surroundings (text over an
        // image, a band of graphics inside the bounds) must not be able to
        // blow the size up or collapse it.
        const double fitted = origInk.height / o.probeInk.heightPerPt;
        currentEditorRenderSizePt =
            qBound(qMax(4.0, currentEditorFontSizePt * 0.6),
                   fitted,
                   qMax(5.0, currentEditorFontSizePt * 1.5));
        if (!sizeIsExact)
            currentEditorFontSizePt = currentEditorRenderSizePt;
    }
}

void EditController::anchorEditOrigin(EditOpen &o)
{
    // Anchor for the replacement text: where the original actually starts,
    // x on the pen and y on the BASELINE of its FIRST line. The content scanner
    // reads it straight out of the text matrix; where no scanner exists
    // (Poppler), the probe in chooseEditorFont() just measured how far our own
    // font puts ink above and right of its pen, so the first line's measured
    // ink position converts back into a pen position.
    activeEditLineSpacingPt = o.isSessionEdit ? o.sessionEdit.lineSpacingPt
                                                : o.contentItem.lineSpacingPt;

    activeEditHasOrigin = false;
    if (o.isSessionEdit) {
        activeEditHasOrigin = o.sessionEdit.hasTextOrigin;
        o.textOrigin = o.sessionEdit.pdfBounds.topLeft()
                     + o.sessionEdit.textOriginOffset;
    } else if (!o.contentItem.textOrigin.isNull()
               && o.contentItem.textOrigin.y() >= activeEditBounds.top() - 1.0
               && o.contentItem.textOrigin.y()
                      <= activeEditBounds.top()
                             + currentEditorRenderSizePt * 1.6) {
        // Guarded against a paragraph whose first line is not the one the
        // scanner reported — anchoring line 1 to line 3's baseline would drop
        // the whole block down the page.
        activeEditHasOrigin = true;
        o.textOrigin = o.contentItem.textOrigin;
    } else if (o.measurable && o.probeInk.risePerPt > 0.05) {
        const InkMetrics::MeasuredInk first = InkMetrics::measuredInkPt(
            o.sampleImage(),
            QRectF(activeEditBounds.left(), activeEditBounds.top(),
                   activeEditBounds.width(),
                   qMin(activeEditBounds.height(),
                        currentEditorRenderSizePt * 1.5)),
            kSampleScale);
        if (first.height > 1.0) {
            activeEditHasOrigin = true;
            o.textOrigin = QPointF(
                first.left - o.probeInk.bearingPerPt * currentEditorRenderSizePt,
                first.top  + o.probeInk.risePerPt    * currentEditorRenderSizePt);
        }
    }
}

void EditController::fitEditHeight(EditOpen &o)
{
    // Expand edit bounds to cover the full rendered line height.  Glyph
    // polygons capture only the inked area (cap height ≈ 72% of font size);
    // large headings can leave original characters peeking out beneath the
    // blank fill without this. Sizes here are the ones being PAINTED — this
    // is about what the frame has to cover on screen.
    if (currentEditorRenderSizePt <= 0) return;

    const double minH = currentEditorRenderSizePt * 1.15;
    if (activeEditBounds.height() < minH) {
        activeEditBounds.setHeight(minH);
        clampToPdfPage(activeEditPage, activeEditBounds);
    }
    // Hard cap: no single-line edit frame should exceed 5× the detected
    // font size — catches polygon bounds that span whole table blocks.
    // Multi-line paragraph bounds from the region model are trusted.
    if (!o.paragraphBounds) {
        const double capH = currentEditorRenderSizePt * 5.0;
        if (activeEditBounds.height() > capH) {
            activeEditBounds.setHeight(capH);
            clampToPdfPage(activeEditPage, activeEditBounds);
        }
    }
    activeEditOriginalBounds = activeEditBounds;
}

void EditController::sampleEditColors(EditOpen &o)
{
    // Text color: stored session color > exact content-stream fill color >
    // pixel sampling of the rendered page (last resort).
    if (o.isSessionEdit && o.sessionEdit.textColor.isValid()) {
        currentEditorColor = o.sessionEdit.textColor;
    } else if (!o.isSessionEdit && o.contentItem.isValid()
               && o.contentItem.textColor.isValid()) {
        currentEditorColor = o.contentItem.textColor;
    } else if (!o.sampleImage().isNull()) {
        const QRectF px(o.block.pdfBounds.topLeft() * kSampleScale,
                        o.block.pdfBounds.size() * kSampleScale);
        currentEditorColor = InkMetrics::sampleTextColor(o.sampleImage(),
                                                           px.toAlignedRect());
    }

    // Background color: stored session color > content-stream fill (light
    // fills only — dark detections are usually borders, not backgrounds) >
    // pixel sampling inside the bounds. Sampling sees the ACTUAL pixels, so it
    // also covers backgrounds the stream scan can't reach (form XObjects,
    // shadings, images) and the Poppler backend, which has no stream scan at
    // all. Without it the blank fill paints white bars over colored table rows.
    currentBgColor = Qt::white;
    if (o.isSessionEdit && o.sessionEdit.bgColor.isValid()) {
        currentBgColor = o.sessionEdit.bgColor;
        return;
    }
    QColor bg;
    if (o.contentItem.isValid() && o.contentItem.bgColor.isValid()) {
        const QColor c   = o.contentItem.bgColor;
        const qreal  lum = 0.299 * c.redF() + 0.587 * c.greenF()
                         + 0.114 * c.blueF();
        if (lum >= 0.70) bg = c;
    }
    if (!bg.isValid() && !o.sampleImage().isNull()) {
        const QRectF px(activeEditBounds.topLeft() * kSampleScale,
                        activeEditBounds.size() * kSampleScale);
        bg = InkMetrics::sampleBackgroundColor(o.sampleImage(), px.toAlignedRect());
    }
    if (bg.isValid()) currentBgColor = bg;
}

void EditController::fitEditWidth(EditOpen &o)
{
    // The editor font's metrics differ slightly from the PDF font. If the
    // detected bounds are even a few points too narrow for the editor font,
    // QTextEdit wraps the last word onto a bogus new line — and the commit
    // paints that wrap into the document. Widen the edit bounds so every
    // original line fits the editor font on ONE line (clamped to the page).
    if (currentEditorRenderSizePt <= 0 || o.displayText.isEmpty()) return;

    QFont measure(currentEditorFontFamily.isEmpty()
                      ? QStringLiteral("Helvetica")
                      : currentEditorFontFamily);
    measure.setStyleHint(QFont::SansSerif);
    // 1 px == 1 pt here
    measure.setPixelSize(qMax(1, qRound(currentEditorRenderSizePt)));
    measure.setBold(currentEditorBold);
    measure.setItalic(currentEditorItalic);
    const QFontMetricsF fm(measure);
    qreal needW = 0.0;
    const QStringList lines = o.displayText.split(u'\n');
    for (const QString &ln : lines)
        needW = qMax(needW, fm.horizontalAdvance(ln));
    needW += currentEditorRenderSizePt * 0.6;   // caret/AA headroom
    if (needW > activeEditBounds.width())
        activeEditBounds.setWidth(
            qMin(needW, o.pageSize.width() - activeEditBounds.left() - 2.0));
}

void EditController::presentEditor(EditOpen &o)
{
    // Freeze the anchor against the final box. Stored as an offset, it now
    // rides along with every later move or resize of the frame for free.
    activeEditOriginOffset = activeEditHasOrigin
                                   ? o.textOrigin - activeEditBounds.topLeft()
                                   : QPointF();

    m_hover->hide();

    // Erasure targets: ONLY the tight glyph rects of the original text.
    // A whole-bounds erase would destroy graphics (chart bars, images,
    // rules) sharing the rectangle with the text.
    activeEditEraseRects.clear();
    if (activeEditNeedsBlank) {
        if (auto *backend = m_src->backend())
            activeEditEraseRects = backend->glyphRects(
                o.block.page, activeEditOriginalBounds, o.erasedZones);
    }

    // Recompute canvas bounds from the (possibly expanded) activeEditBounds so
    // both the blank fill rect and the editor frame cover the full rendered text.
    const QRectF canvasBounds(
        activeEditBounds.topLeft() * o.scale + QPointF(o.label->pos()),
        activeEditBounds.size() * o.scale);
    const int fontSize = qMax(6, qRound(currentEditorRenderSizePt * o.scale));

    m_frame->setDecorations(true);
    m_frame->setForbiddenZones({});
    m_frame->setPageRect(o.label->geometry());
    // Single-line edits extend horizontally while typing instead of wrapping.
    m_frame->setGrowHorizontal(!o.displayText.contains(u'\n'));
    currentBox.bounds = activeEditBounds;
    m_frame->setBoxProperties(currentBox, o.scale);
    m_frame->resetCommitGuard();
    undoSnapBefore = m_session->snapshotEdits();
    m_session->suspendEditsAt(o.block.page, o.block.pdfBounds);
    if (activeEditNeedsBlank)
        Q_EMIT pageNeedsBlank(activeEditPage, activeEditBounds);
    else
        Q_EMIT pageNeedsRerender(activeEditPage);
    m_frame->present(o.displayText, canvasBounds, fontSize,
                           currentEditorColor, currentEditorFontFamily,
                           currentEditorBold, currentEditorItalic);

    // Reference state for the "nothing changed" check in commitCurrentEdit.
    // Captured AFTER present(): the frame grows to fit its content on open and
    // reports the grown geometry back through boundsChanged, so this is the
    // resting state — any later deviation really is the user's doing.
    activeEditInPlace             = true;
    activeEditPresentedBounds     = activeEditBounds;
    activeEditPresentedFontSizePt = currentEditorFontSizePt;
    activeEditPresentedColor      = currentEditorColor;
    presentedBox                  = textBoxProperties();
    Q_EMIT textBoxPropertiesChanged(presentedBox);
    Q_EMIT textBoxEditingChanged(true);
}


// Opening an edit in place: resolve what was clicked, work out how the
// replacement text has to look, then put the editor over it. Each step reads
// what the previous ones decided — see EditOpen.
void EditController::handleClick(const QPoint &canvasPos)
{
    EditOpen o;
    if (!resolveEditTarget(canvasPos, o)) return;

    applyEditTargetBounds(o);
    chooseEditorFont(o);
    anchorEditOrigin(o);

    Q_EMIT fontSizeChanged(qRound(currentEditorFontSizePt));
    Q_EMIT fontChanged(currentEditorFontFamily.isEmpty()
                                 ? QStringLiteral("Helvetica")
                                 : currentEditorFontFamily,
                             currentEditorBold, currentEditorItalic);

    fitEditHeight(o);
    // Colors are read between the two fits on purpose: the background sample
    // must cover the full line height, but not the width the editor font
    // needs — that reaches into untouched page area.
    sampleEditColors(o);
    fitEditWidth(o);

    presentEditor(o);
}

EditSession::Edit EditController::makeSessionEdit(int page, const QRectF &bounds,
                                                const QRectF &sourceRect,
                                                const QString &text) const
{
    EditSession::Edit e;
    e.page        = page;
    e.pdfBounds   = bounds;
    e.sourceRect  = sourceRect;
    e.newText     = text;                       // null → blank (erase-only) edit
    e.fontSizePt  = currentEditorFontSizePt;
    e.renderSizePt = currentEditorRenderSizePt;
    e.textOriginOffset = activeEditOriginOffset;
    e.hasTextOrigin    = activeEditHasOrigin;
    e.lineSpacingPt    = activeEditLineSpacingPt;
    e.originalText     = activeEditPdfText;
    e.textColor   = currentEditorColor;
    e.bgColor     = currentBgColor;
    e.fontFamily  = currentEditorFontFamily;
    e.bold        = currentEditorBold;
    e.italic      = currentEditorItalic;
    e.fontChanged = editorFontChangedByUser;
    e.sizeChanged = editorSizeChangedByUser;
    e.formField   = activeEditFieldName;
    e.box         = currentBox;
    e.box.bounds  = bounds;
    return e;
}
#endif

void EditController::commit(const QString &newText)
{
#ifdef HAVE_PDF_RENDERING
    if (activeEditPage < 0) return;

    // Capture all state before hide() — hide() can trigger a recursive
    // focusOut→committed→commitCurrentEdit call, which exits early because
    // activeEditPage is already -1.
    const int    page       = activeEditPage;         // where the box is NOW
    const int    srcPage    = activeEditSourcePage >= 0 ? activeEditSourcePage
                                                          : activeEditPage;
    const QRectF bounds     = activeEditBounds;
    const QRectF origBounds = activeEditOriginalBounds;
    activeEditPage       = -1;
    activeEditSourcePage = -1;

    m_frame->hide();  // may trigger recursive commit, which exits early ↑
    Q_EMIT textBoxEditingChanged(false);

    const QString trimNew = newText.trimmed();

    // Nothing changed → drop the edit instead of committing it. Committing
    // would erase the original glyphs and re-draw the text with OUR font, so a
    // click that only opened and closed the editor would visibly rewrite the
    // line (different family, ligatures and umlauts gone if the text came from
    // OCR). The document must stay byte-identical unless the user really
    // edited something.
    if (activeEditInPlace) {
        // Bounds come back from integer widget geometry, so compare with a
        // tolerance of about two screen pixels — anything the user actually
        // dragged or resized moves much further than that.
        const double tol = qMax(1.0, 2.0 / PdfRenderer::screenScale(m_zoom->zoom()));
        const auto nearly = [tol](const QRectF &a, const QRectF &b) {
            return std::abs(a.left()   - b.left())   < tol
                && std::abs(a.top()    - b.top())    < tol
                && std::abs(a.width()  - b.width())  < tol
                && std::abs(a.height() - b.height()) < tol;
        };
        const bool untouched =
               page == srcPage
            && trimNew == activeEditOriginalText.trimmed()
            && nearly(bounds, activeEditPresentedBounds)
            && !editorFontChangedByUser
            && qFuzzyCompare(currentEditorFontSizePt + 1.0,
                             activeEditPresentedFontSizePt + 1.0)
            && currentEditorColor      == activeEditPresentedColor;
        const bool boxUntouched = textBoxProperties() == presentedBox;
        if (untouched && boxUntouched) {
            activeEditInPlace = false;
            activeEditFieldName.clear();
            m_session->restoreSuspended();   // undo the open-time suspension
            lastCommittedPage = -1;        // nothing committed to protect
            Q_EMIT pageNeedsRerender(srcPage);           // drops the live blank fill
            return;
        }
    }
    activeEditInPlace = false;

    // undoSnapBefore was captured in handleEditClick/createTextFrame — BEFORE
    // suspendEditsAt() and before any live edits — so it is the true pre-edit state.
    const auto snapBefore = undoSnapBefore;

    // Every commit creates a tracked session entry so that overlapping blocks
    // can each be addressed independently in the same session.
    // Suspended edits are discarded — we replace them with the new content.
    m_session->clearSuspended();
    // Remove the live edit and any stale edit at origBounds using EXACT match.
    // removeAllAt (intersects) is intentionally avoided here: a full paragraph's
    // pdfBounds can span several hundred pixels, so it would accidentally delete
    // adjacent session edits that are close but distinct.
    m_session->removeEdit(srcPage, origBounds);
    if (page != srcPage || !bounds.intersects(origBounds))
        m_session->removeEdit(page, bounds);

    // Blank MUST be inserted before the text so applyToImage erases origBounds
    // before drawing the new text.  It is needed when:
    //   • editing existing text (handleEditClick) — erase original PDF or session text
    //   • text was moved to a new position — erase original location
    //   • text was deleted entirely
    // It is NOT created when the editor was opened fresh via drag (createTextFrame)
    // because that is a transparent overlay: new text drawn on top without erasing.
    // The second condition must be gated on activeEditNeedsBlank — for fresh drag
    // boxes the origBounds is just the initial drag rect, not real PDF content to erase.
    const bool needBlank = activeEditNeedsBlank;
    if (needBlank) {
        EditSession::Edit blank = makeSessionEdit(srcPage, origBounds, origBounds,
                                                  QString());
        blank.eraseRects = activeEditEraseRects;   // glyphs only, not the rect
        m_session->addEdit(std::move(blank));
    }

    if (!trimNew.isEmpty())
        m_session->addEdit(makeSessionEdit(page, bounds, origBounds, newText));
    activeEditFieldName.clear();

    // Snapshot AFTER state.  Only push an undo command if the session actually
    // changed — avoid polluting the stack with no-op "clicked and walked away" entries.
    const auto snapAfter = m_session->snapshotEdits();
    if (snapAfter != snapBefore) {
        // A push moves the stack index too, but it is a NEW state — the
        // history must append it, not look for one it already knows.
        pushingEdit = true;
        m_undo->push(new EditUndoCmd(m_session, this, srcPage, page,
                                          snapBefore, snapAfter));
        pushingEdit = false;
        // Recorded after the push so the entry carries the stack index the
        // state sits at — that index is what takes the document back here.
        Q_EMIT changeRecorded({ trimNew.isEmpty() ? DocumentHistory::Kind::TextRemoved
                                         : DocumentHistory::Kind::TextEdited,
                       page });
    }

    // Remember the original PDF block bounds so that the mouseRelease handler
    // for THIS SAME CLICK can avoid re-opening an editor for the block we just
    // committed — doing so would call rerenderPageWithBlank and blank the text.
    lastCommittedPage       = srcPage;
    lastCommittedOrigBounds = origBounds;

    // When blanking is needed, rerenderPageWithBlank ensures origBounds is
    // cleared via BOTH applyToImage (session blank edit) and the explicit fill —
    // belt-and-suspenders so the original text definitely disappears.
    if (needBlank)
        Q_EMIT pageNeedsBlank(srcPage, origBounds);
    else
        Q_EMIT pageNeedsRerender(srcPage);
    if (page != srcPage)
        Q_EMIT pageNeedsRerender(page);   // box was dragged onto another page — paint it there
#else
    Q_UNUSED(newText)
#endif
}

void EditController::cancel()
{
#ifdef HAVE_PDF_RENDERING
    const int page    = activeEditPage;
    const int srcPage = activeEditSourcePage;
    activeEditPage       = -1;
    activeEditSourcePage = -1;
    activeEditInPlace    = false;
    m_session->restoreSuspended();
    m_frame->hide();
    Q_EMIT textBoxEditingChanged(false);
    if (page >= 0)
        Q_EMIT pageNeedsRerender(page);
    if (srcPage >= 0 && srcPage != page)
        Q_EMIT pageNeedsRerender(srcPage);
#else
    activeEditPage = -1;
#endif
}

// ── Drag-to-create text frame ─────────────────────────────────────────────────

void EditController::createTextFrame(const QRect &viewportDragRect)
{
#ifdef HAVE_PDF_RENDERING
    // The canvas is a child of the viewport and scrolling moves it, so its
    // negated position is the scroll offset — readable without the scroll area.
    const QPoint scroll = -m_canvas->canvasWidget()->pos();
    const QRect canvasRect = viewportDragRect.translated(scroll);

    auto [pageIdx, pageLbl] = m_canvas->pageAtCanvasPos(canvasRect.center());
    if (pageIdx < 0) return;

    const qreal scale = PdfRenderer::screenScale(m_zoom->zoom());
    activeEditPage       = pageIdx;
    activeEditSourcePage = pageIdx;
    activeEditBounds = QRectF(
        (QPointF(canvasRect.topLeft()) - QPointF(pageLbl->pos())) / scale,
        QSizeF(canvasRect.size()) / scale);
    clampToPdfPage(pageIdx, activeEditBounds);
    activeEditOriginalBounds = activeEditBounds;
    activeEditOriginalText  = QString();
    activeEditPdfText       = QString();  // nothing replaced → no font to keep
    activeEditInPlace       = false;   // fresh box — there is nothing to leave alone
    activeEditNeedsBlank    = false;   // new text box overlay — don't erase background
    activeEditEraseRects.clear();
    activeEditFieldName.clear();
    undoSnapBefore          = m_session->snapshotEdits();  // before any live edits

    currentEditorColor = QColor(0x11, 0x11, 0x11);
    currentBgColor     = Qt::white;  // drag-created box: transparent overlay, no erasure
    // Fresh text box: default font, nothing detected to inherit.
    currentEditorFontFamily.clear();
    currentEditorBold       = false;
    currentEditorItalic     = false;
    editorFontChangedByUser = false;
    editorSizeChangedByUser = false;
    currentEditorFontSizePt   = 12.0;
    currentEditorRenderSizePt = 12.0;
    // A box drawn on empty canvas has no original text to sit on the line of.
    activeEditHasOrigin     = false;
    activeEditOriginOffset  = QPointF();
    activeEditLineSpacingPt = 0.0;
    currentBox = defaultBox;
    currentBox.bounds = activeEditBounds;
    Q_EMIT fontSizeChanged(qRound(currentEditorFontSizePt));
    Q_EMIT fontChanged(QStringLiteral("Helvetica"), false, false);

    m_hover->hide();
    const int fontSize = qMax(8, qRound(12.0 * scale));
    m_frame->setDecorations(true);  // new text box: show border + handles
    m_frame->setGrowHorizontal(false);   // user chose this width
    m_frame->setBoxProperties(currentBox, scale);
    m_frame->setPageRect(pageLbl->geometry());
    m_frame->setForbiddenZones({});
    m_frame->resetCommitGuard();
    m_frame->present(QString(), QRectF(canvasRect), fontSize, currentEditorColor);
    presentedBox = textBoxProperties();
    Q_EMIT textBoxPropertiesChanged(presentedBox);
    Q_EMIT textBoxEditingChanged(true);
#else
    Q_UNUSED(viewportDragRect)
#endif
}
