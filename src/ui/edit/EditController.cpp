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
#  include <QRawFont>
#  include <QLabel>
#  include <QStringList>
#  include <QUndoStack>
#  include <QWidget>
#  include <algorithm>
#endif

/// Stores undo/redo snapshots for a text edit command.
class EditUndoCmd : public QUndoCommand
{
    EditSession              *m_session;
    EditController           *m_ctl;
    int                       m_srcPage, m_dstPage;
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
    refreshAdvanceMeasure();
    m_frame->setTextFont(currentEditorFontFamily, currentEditorBold,
                               currentEditorItalic, currentEditorUnderline);
    syncBoundsFromFrame();
    notifyBoundsChanged();
    refreshLivePreview();
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

void EditController::setUnderline(bool on)
{
#ifdef HAVE_PDF_RENDERING
    if (on == currentEditorUnderline) return;
    currentEditorUnderline = on;

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

        if (currentEditorRenderSizePt > 0 && ptSize != currentEditorRenderSizePt) {
            const qreal ratio = (qreal)ptSize / currentEditorRenderSizePt;
            const QPointF anchor = activeEditBounds.topLeft();
            activeEditBounds = QRectF(anchor, activeEditBounds.size() * ratio);

            if (m_src->renderer()) {
                const qreal seite =
                    m_src->renderer()->pageSizePts(activeEditPage).width();
                if (activeEditBounds.right() > seite)
                    activeEditBounds.setRight(qMax(anchor.x() + 1.0, seite));
            }
            clampToPdfPage(activeEditPage, activeEditBounds);

            activeEditLineSpacingPt *= ratio;
        }

        currentEditorFontSizePt   = ptSize;
        currentEditorRenderSizePt = ptSize;
        editorSizeChangedByUser   = true;
        refreshAdvanceMeasure();
        const QLabel *lbl = m_canvas->pageLabel(activeEditPage);
        if (lbl) {
            m_frame->setPageRect(lbl->geometry());
            const QRectF cb(
                activeEditBounds.topLeft() * scale + QPointF(lbl->pos()),
                activeEditBounds.size() * scale);
            m_frame->repositionForZoom(cb, qMax(1.0, ptSize * scale),
                                       currentBox, scale);
        }

        m_frame->setLineSpacingPt(activeEditLineSpacingPt);

        syncBoundsFromFrame();
        notifyBoundsChanged();
        refreshLivePreview();
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
    if (activeEditPage >= 0 && m_frame->isVisible()) {
        m_frame->setTextColor(color);
        refreshAdvanceMeasure();
        refreshLivePreview();
    }
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

void EditController::refreshAdvanceMeasure()
{
#ifdef HAVE_PDF_RENDERING
    auto *backend = m_src->backend();
    const bool eigene = editorFontChangedByUser || activeEditPdfText.isEmpty();
    m_frame->setStandardFace(eigene && !(backend && backend->canEmbedFont(
        currentEditorFontFamily, currentEditorBold, currentEditorItalic)));
    if (!backend) { m_frame->setMetricsSource({}); return; }
    m_frame->setMetricsSource([this, backend](const QString &text) {
        if (activeEditPage < 0) return TextLayout::Metrics();
        const QRectF eraseAt = activeEditEraseBounds.isNull() ? activeEditOriginalBounds
                                                              : activeEditEraseBounds;
        m_metricsPage   = activeEditPage;
        m_metricsBounds = activeEditBounds;
        EditSession::Edit edit = makeSessionEdit(activeEditPage, activeEditBounds,
                                                 eraseAt, text);
        if (activeEditSourcePage >= 0 && activeEditSourcePage != activeEditPage)
            edit.sourceRect = QRectF();
        return backend->editMetrics(edit);
    });
#endif
}

void EditController::syncBoundsFromFrame()
{
#ifdef HAVE_PDF_RENDERING
    if (activeEditPage < 0 || !m_frame->isVisible()) return;
    const QLabel *lbl = m_canvas->pageLabel(activeEditPage);
    if (!lbl) return;
    const qreal scale = PdfRenderer::screenScale(m_zoom->zoom());
    const QRectF inner = m_frame->innerCanvasRect();
    QRectF b((inner.topLeft() - QPointF(lbl->pos())) / scale,
             inner.size() / scale);
    clampToPdfPage(activeEditPage, b);
    activeEditBounds  = b;
    currentBox.bounds = b;
#endif
}

void EditController::notifyBoundsChanged()
{
    if (activeEditPage >= 0)
        Q_EMIT textBoxPropertiesChanged(textBoxProperties());
}

void EditController::refreshLivePreview()
{
#ifdef HAVE_PDF_RENDERING
    if (activeEditPage < 0) { Q_EMIT livePreviewChanged(-1, {}); return; }

    for (int i = 0; i < 4 && m_frame->isVisible() && !m_refreshingMetrics
                    && (m_metricsPage != activeEditPage || m_metricsBounds != activeEditBounds); ++i) {
        m_refreshingMetrics = true;
        m_metricsPage   = activeEditPage;
        m_metricsBounds = activeEditBounds;
        m_frame->invalidateMetrics();
        m_refreshingMetrics = false;
    }
    const QString text = m_frame->currentText();
    QList<EditSession::Edit> edits;
    if (activeEditFieldName.isEmpty() && !text.isEmpty()) {
        const QRectF eraseAt = activeEditEraseBounds.isNull()
                                   ? activeEditOriginalBounds : activeEditEraseBounds;
        edits.append(makeSessionEdit(activeEditPage, activeEditBounds, eraseAt, text));
    }
    Q_EMIT livePreviewChanged(activeEditPage, edits);
#endif
}

void EditController::setTextBoxProperties(const TextBoxProperties &properties)
{
#ifdef HAVE_PDF_RENDERING
    if (activeEditPage < 0 || !m_frame->isVisible()) {

        const QRectF defaultBounds = defaultBox.bounds;
        defaultBox = properties;
        defaultBox.bounds = defaultBounds;
        return;
    }
    currentBox = properties;
    refreshAdvanceMeasure();
    activeEditBounds = properties.bounds.normalized();
    if (activeEditBounds.width() < 1.0) activeEditBounds.setWidth(1.0);
    if (activeEditBounds.height() < 1.0) activeEditBounds.setHeight(1.0);
    clampToPdfPage(activeEditPage, activeEditBounds);
    currentBox.bounds = activeEditBounds;

    const QLabel *lbl = m_canvas->pageLabel(activeEditPage);
    if (!lbl) return;
    const qreal scale = PdfRenderer::screenScale(m_zoom->zoom());
    m_frame->setPageRect(lbl->geometry());
    const QRectF cb(activeEditBounds.topLeft() * scale + QPointF(lbl->pos()),
                    activeEditBounds.size() * scale);
    m_frame->repositionForZoom(
        cb, qMax(1.0, currentEditorRenderSizePt * scale),
        currentBox, scale);
    syncBoundsFromFrame();
    refreshLivePreview();
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
    if (activeEditPage >= 0) m_frame->setLineSpacingPt(activeEditLineSpacingPt);
    setTextBoxProperties(p);
}

void EditController::clampToPdfPage(int page, QRectF &r) const
{
#ifdef HAVE_PDF_RENDERING
    if (!m_src->renderer() || page < 0) return;
    const QSizeF ps = m_src->renderer()->pageSizePts(page);

    r = r.normalized();
    if (r.width()  > ps.width())  r.setWidth(ps.width());
    if (r.height() > ps.height()) r.setHeight(ps.height());

    if (r.left()   < 0)           r.moveLeft(0);
    if (r.top()    < 0)           r.moveTop(0);
    if (r.right()  > ps.width())  r.moveRight(ps.width());
    if (r.bottom() > ps.height()) r.moveBottom(ps.height());
#else
    Q_UNUSED(page) Q_UNUSED(r)
#endif
}

#ifdef HAVE_PDF_RENDERING

static constexpr qreal kSampleScale = 3.0;

struct EditController::EditOpen
{

    int      page  { -1 };
    QLabel  *label { nullptr };
    QPointF  pdfPt;
    QSizeF   pageSize;
    qreal    scale { 1.0 };
    QList<QRectF> erasedZones;

    bool              isSessionEdit { false };
    EditSession::Edit sessionEdit;
    int               blankPage { -1 };
    TextBlock         block;
    ContentItem       contentItem;
    QString           displayText;

    bool              paragraphBounds { false };

    bool                measurable { false };
    InkMetrics::FontInk probeInk;
    QPointF             textOrigin;

    PdfRenderer *renderer { nullptr };

    bool canSample() const { return renderer && renderer->backend() && block.page >= 0; }

    QColor sampleTextColor(const QRectF &pt)
    {
        const QRect px = pixelsOf(pt);
        return cover(px) ? InkMetrics::sampleTextColor(m_samp, px.translated(-m_area.topLeft()))
                         : InkMetrics::sampleTextColor(QImage(), px);
    }

    QColor sampleBackgroundColor(const QRectF &pt)
    {
        const QRect px = pixelsOf(pt);
        return cover(px) ? InkMetrics::sampleBackgroundColor(m_samp,
                                                             px.translated(-m_area.topLeft()))
                         : QColor();
    }

    InkMetrics::MeasuredInk measuredInk(const QRectF &pt)
    {
        const qreal pad = qMax(4.0, pt.height());
        if (!cover(pixelsOf(pt.adjusted(-pad, -pad, pad, pad)))) return {};
        const QPointF origin = QPointF(m_area.topLeft()) / kSampleScale;
        InkMetrics::MeasuredInk ink =
            InkMetrics::measuredInkPt(m_samp, pt.translated(-origin), kSampleScale);
        if (ink.height > 0.0) {
            ink.top  += origin.y();
            ink.left += origin.x();
        }
        return ink;
    }

private:
    static QRect pixelsOf(const QRectF &pt)
    {
        return QRectF(pt.topLeft() * kSampleScale, pt.size() * kSampleScale).toAlignedRect();
    }

    // Renders the unedited page around the parts that are sampled instead of
    // the whole page at sample scale, which on heavy pages took a second.
    bool cover(const QRect &px)
    {
        if (!canSample()) return false;
        const QRect pageRect(QPoint(), renderer->backend()->pixelSize(block.page, kSampleScale));
        const QRect need = px.intersected(pageRect);
        if (need.isEmpty() || (!m_samp.isNull() && m_area.contains(need))) return !m_samp.isNull();
        const int margin = qRound(24 * kSampleScale);
        const QRect area = (m_samp.isNull() ? need : m_area.united(need))
                               .adjusted(-margin, -margin, margin, margin).intersected(pageRect);
        const PdfBackend::AreaRender render =
            renderer->backend()->renderChanges(block.page, kSampleScale, nullptr, area, false);
        if (render.image.isNull()) return false;
        m_samp = render.image;
        m_area = render.pixels;
        return true;
    }

    QImage m_samp;
    QRect  m_area;
};

bool EditController::resolveEditTarget(const QPoint &canvasPos, EditOpen &o)
{
    auto [pageIdx, pageLbl] = m_canvas->pageAtCanvasPos(canvasPos);
    if (pageIdx < 0) return false;

    o.page     = pageIdx;
    o.label    = pageLbl;
    o.scale    = PdfRenderer::screenScale(m_zoom->zoom());
    o.pdfPt    = QPointF(canvasPos - pageLbl->pos()) / o.scale;
    o.pageSize = m_src->renderer()->pageSizePts(pageIdx);
    o.renderer = m_src->renderer();

    o.erasedZones = m_session->blankRegions(pageIdx);

    if (m_session->findEditAt(pageIdx, o.pdfPt, &o.sessionEdit)) {
        o.block         = TextBlock{ pageIdx, o.sessionEdit.pdfBounds,
                                     o.sessionEdit.newText };
        o.isSessionEdit = true;
    }

    if (!o.isSessionEdit && m_src->contentProvider())
        o.contentItem = m_src->contentProvider()->itemAt(pageIdx, o.pdfPt);

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

    if (!o.isSessionEdit && o.contentItem.isFormField())
        o.block = TextBlock{ pageIdx, o.contentItem.bounds, o.contentItem.text };

    if (!o.block.isValid()) {
        if (auto *backend = m_src->backend())
            o.block = backend->textAt(pageIdx, o.pdfPt, o.erasedZones);
    }

    if (!o.block.isValid() && m_ocr && m_ocr->isReady()
            && m_src->backend() && !m_src->backend()->hasSelectableText(pageIdx)) {
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

    if (!o.block.isValid() && o.contentItem.isValid() && o.contentItem.isTextual()
            && !o.contentItem.text.isEmpty())
        o.block = TextBlock{ pageIdx, o.contentItem.bounds, o.contentItem.text };

    if (!o.block.isValid()) return false;

    if (o.block.page == lastCommittedPage &&
        o.block.pdfBounds.intersects(lastCommittedOrigBounds)) {
        lastCommittedPage = -1;
        return false;
    }
    lastCommittedPage = -1;

    if (!o.isSessionEdit && (m_session->isBlankAt(o.block.page, o.pdfPt)
                             || m_session->isBlankCovering(o.block.page,
                                                           o.block.pdfBounds)))
        return false;

    if (o.isSessionEdit) {
        o.pdfPt = o.block.pdfBounds.center();
    } else if (!o.contentItem.isFormField()) {
        o.pdfPt = o.block.pdfBounds.center();
        if (m_src->contentProvider()) {
            const ContentItem atBlock =
                m_src->contentProvider()->itemAt(o.block.page, o.pdfPt);
            o.contentItem = atBlock.isFormField() ? ContentItem{} : atBlock;
        }
    }

    if (o.contentItem.isValid() && !o.contentItem.isFormField()
            && !o.contentItem.bounds.intersects(o.block.pdfBounds))
        o.contentItem = ContentItem{};

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
    activeEditNeedsBlank     = true;
    activeEditEraseBounds    = activeEditBounds;
    if (o.isSessionEdit) {
        activeEditNeedsBlank  = !o.sessionEdit.eraseRects.isEmpty();
        activeEditEraseBounds = o.sessionEdit.sourceRect.isNull()
                                    ? activeEditBounds : o.sessionEdit.sourceRect;
        for (const EditSession::Edit &e : m_session->edits()) {
            if (!e.newText.isNull() || e.pdfBounds != activeEditEraseBounds
                    || e.formField != o.sessionEdit.formField)
                continue;
            if (o.blankPage < 0 || e.page == o.block.page) o.blankPage = e.page;
        }
        if (o.blankPage >= 0) activeEditSourcePage = o.blankPage;
    }
    currentBox = o.isSessionEdit ? o.sessionEdit.box : TextBoxProperties{};
    activeEditFieldName.clear();

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

            o.paragraphBounds = true;
            TextBlock para;
            if (auto *backend = m_src->backend())
                para = backend->blockInRect(o.block.page, o.contentItem.bounds,
                                            o.erasedZones);

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
                o.paragraphBounds = false;
            }
        }
        if (o.contentItem.isFormField()) {
            activeEditFieldName = o.contentItem.fieldName;
            o.displayText         = o.contentItem.text;

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

    bool sizeIsExact = false;
    editorSizeChangedByUser = false;
    if (o.isSessionEdit && o.sessionEdit.fontSizePt > 0.0) {
        currentEditorFontSizePt = qMax(4.0, o.sessionEdit.fontSizePt);
        editorSizeChangedByUser = o.sessionEdit.sizeChanged;
        sizeIsExact = true;
    } else {
        const double detectedPt = (o.contentItem.isValid()
                                   && o.contentItem.fontSizePt > 0.0)
                                      ? o.contentItem.fontSizePt : 0.0;
        if (detectedPt > 0.0 && detectedPt <= 144.0) {
            const double polyEst   = o.block.pdfBounds.height() / 0.72;
            const bool   plausible = (detectedPt <= polyEst * 4.0)
                                   || (o.block.pdfBounds.height() >= 20.0);
            if (plausible) {

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

    if (!o.isSessionEdit && !o.contentItem.isFormField()) {
        const double lineGlyphH = o.block.pdfBounds.height();
        if (lineGlyphH > 4.0 && lineGlyphH < 60.0
                && currentEditorFontSizePt > lineGlyphH * 1.5) {
            currentEditorFontSizePt = qMax(4.0, lineGlyphH * 1.05);
            sizeIsExact = false;
        }
    }

    if (o.isSessionEdit) {
        currentEditorFontFamily = o.sessionEdit.fontFamily;
        currentEditorBold       = o.sessionEdit.bold;
        currentEditorItalic     = o.sessionEdit.italic;
        currentEditorUnderline  = o.sessionEdit.underline;
        editorFontChangedByUser = o.sessionEdit.fontChanged;
    } else if (!o.contentItem.fontFamily.isEmpty()) {
        currentEditorFontFamily = o.contentItem.fontFamily;
        currentEditorBold       = o.contentItem.bold;
        currentEditorItalic     = o.contentItem.italic;
        currentEditorUnderline  = false;
        editorFontChangedByUser = false;
    } else {
        currentEditorFontFamily.clear();
        currentEditorBold       = false;
        currentEditorItalic     = false;
        currentEditorUnderline  = false;
        editorFontChangedByUser = false;
    }

    if (!o.isSessionEdit && !o.contentItem.isFormField()) {
        if (auto *backend = m_src->backend()) {
            const QString embedded =
                backend->embeddedFontFamily(o.block.page, o.pdfPt);
            if (!embedded.isEmpty()) currentEditorFontFamily = embedded;
        }
    }

    currentEditorRenderSizePt = currentEditorFontSizePt;
    o.measurable = !sizeIsExact && !o.isSessionEdit && !o.contentItem.isFormField()
                && !o.displayText.isEmpty() && o.canSample();
    if (!o.measurable) return;

    const InkMetrics::MeasuredInk origInk = o.measuredInk(activeEditBounds);
    QFont probe(currentEditorFontFamily.isEmpty()
                    ? QStringLiteral("Helvetica") : currentEditorFontFamily);
    probe.setStyleHint(QFont::SansSerif);
    probe.setBold(currentEditorBold);
    probe.setItalic(currentEditorItalic);
    o.probeInk = InkMetrics::fontInkPerPt(o.displayText, probe);
    if (origInk.height > 1.0 && o.probeInk.heightPerPt > 0.05) {

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

        activeEditHasOrigin = true;
        o.textOrigin = o.contentItem.textOrigin;
    } else if (o.measurable && o.probeInk.risePerPt > 0.05) {
        const InkMetrics::MeasuredInk first = o.measuredInk(
            QRectF(activeEditBounds.left(), activeEditBounds.top(),
                   activeEditBounds.width(),
                   qMin(activeEditBounds.height(),
                        currentEditorRenderSizePt * 1.5)));
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
    if (currentEditorRenderSizePt <= 0) return;

    QFont line(currentEditorFontFamily.isEmpty()
                   ? QStringLiteral("Helvetica") : currentEditorFontFamily);
    line.setStyleHint(QFont::SansSerif);
    line.setPixelSize(qMax(1, qRound(currentEditorRenderSizePt * o.scale)));
    line.setBold(currentEditorBold);
    line.setItalic(currentEditorItalic);
    const QFontMetricsF fm(line);
    const double scale = qMax(0.01, o.scale);

    if (activeEditHasOrigin) {
        const double baseline = o.textOrigin.y();
        activeEditBounds.setTop(qMin(activeEditBounds.top(),
                                     baseline - fm.ascent() / scale));
        activeEditBounds.setBottom(qMax(activeEditBounds.bottom(),
                                        baseline + fm.descent() / scale));
        clampToPdfPage(activeEditPage, activeEditBounds);
    } else if (activeEditBounds.height() < fm.height() / scale) {
        activeEditBounds.setHeight(fm.height() / scale);
        clampToPdfPage(activeEditPage, activeEditBounds);
    }

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

    if (o.isSessionEdit && o.sessionEdit.textColor.isValid()) {
        currentEditorColor = o.sessionEdit.textColor;
    } else if (!o.isSessionEdit && o.contentItem.isValid()
               && o.contentItem.textColor.isValid()) {
        currentEditorColor = o.contentItem.textColor;
    } else if (o.canSample()) {
        currentEditorColor = o.sampleTextColor(o.block.pdfBounds);
    }

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
    if (!bg.isValid() && o.canSample())
        bg = o.sampleBackgroundColor(activeEditBounds);
    if (bg.isValid()) currentBgColor = bg;
}

void EditController::fitEditWidth(EditOpen &o)
{

    if (currentEditorRenderSizePt <= 0 || o.displayText.isEmpty()) return;

    QFont measure(currentEditorFontFamily.isEmpty()
                      ? QStringLiteral("Helvetica")
                      : currentEditorFontFamily);
    measure.setStyleHint(QFont::SansSerif);

    measure.setPixelSize(qMax(1, qRound(currentEditorRenderSizePt)));
    measure.setBold(currentEditorBold);
    measure.setItalic(currentEditorItalic);
    const QFontMetricsF fm(measure);
    qreal needW = 0.0;
    const QStringList lines = o.displayText.split(u'\n');
    for (const QString &ln : lines)
        needW = qMax(needW, fm.horizontalAdvance(ln));
    needW += 1.5;
    if (needW > activeEditBounds.width())
        activeEditBounds.setWidth(
            qMin(needW, o.pageSize.width() - activeEditBounds.left() - 2.0));
}

void EditController::presentEditor(EditOpen &o)
{

    activeEditOriginOffset = activeEditHasOrigin
                                   ? o.textOrigin - activeEditBounds.topLeft()
                                   : QPointF();

    m_hover->hide();

    if (o.isSessionEdit) {
        activeEditEraseRects = o.sessionEdit.eraseRects;
    } else {
        activeEditEraseRects.clear();
        if (activeEditNeedsBlank) {
            if (auto *backend = m_src->backend())
                activeEditEraseRects = backend->glyphRects(
                    o.block.page, activeEditOriginalBounds, o.erasedZones);
        }
        activeEditEraseBounds = activeEditOriginalBounds;
    }
    activeEditOriginalLines.clear();
    if (o.isSessionEdit) {
        activeEditOriginalLines = o.sessionEdit.originalLines;
    } else if (activeEditNeedsBlank && activeEditFieldName.isEmpty()) {
        if (auto *backend = m_src->backend()) {
            const QList<TextLayout::OriginalLine> lines = backend->originalLines(
                o.block.page, activeEditOriginalBounds, o.erasedZones);
            QStringList texts;
            for (const TextLayout::OriginalLine &line : lines) texts << line.text;
            if (!lines.isEmpty() && texts.join(QLatin1Char('\n')) == o.displayText)
                activeEditOriginalLines = lines;
        }
    }

    const QRectF canvasBounds(
        activeEditBounds.topLeft() * o.scale + QPointF(o.label->pos()),
        activeEditBounds.size() * o.scale);
    const qreal fontSize = qMax(1.0, currentEditorRenderSizePt * o.scale);

    m_frame->setDecorations(true);
    m_frame->setGlyphsVisible(!activeEditFieldName.isEmpty());
    m_frame->setLineSpacingPt(activeEditLineSpacingPt);
    activeEditPdfPt = o.pdfPt;
    refreshAdvanceMeasure();
    m_frame->setTextAnchor(activeEditHasOrigin, activeEditOriginOffset);
    m_frame->setForbiddenZones({});
    m_frame->setPageRect(o.label->geometry());
    m_frame->setGrowHorizontal(!o.isSessionEdit
                               || (!o.sessionEdit.originalLines.isEmpty()
                                   && !o.sessionEdit.newText.contains(QChar(TextLayout::kSoftBreak))));
    currentBox.bounds = activeEditBounds;
    m_frame->setBoxProperties(currentBox, o.scale);
    m_frame->resetCommitGuard();
    undoSnapBefore = m_session->snapshotEdits();
    m_session->suspendEditsAt(o.block.page, o.block.pdfBounds,
                              o.blankPage, activeEditEraseBounds);
    if (activeEditNeedsBlank)
        Q_EMIT pageNeedsBlank(activeEditPage, activeEditEraseBounds);
    else
        Q_EMIT pageNeedsRerender(activeEditPage);
    m_frame->present(o.displayText, canvasBounds, fontSize,
                           currentEditorColor, currentEditorFontFamily,
                           currentEditorBold, currentEditorItalic,
                           currentEditorUnderline);

    activeEditOriginalText        = m_frame->currentText();
    activeEditOriginalPlain       = m_frame->plainText();
    activeEditMovedByUser         = false;
    refreshLivePreview();
    activeEditInPlace             = true;
    activeEditPresentedPage       = activeEditPage;
    activeEditPresentedFamily     = currentEditorFontFamily;
    activeEditPresentedBold       = currentEditorBold;
    activeEditPresentedItalic     = currentEditorItalic;
    activeEditPresentedUnderline  = currentEditorUnderline;
    activeEditPresentedBounds     = activeEditBounds;
    activeEditPresentedFontSizePt = currentEditorFontSizePt;
    activeEditPresentedColor      = currentEditorColor;
    presentedBox                  = textBoxProperties();
    refreshAdvanceMeasure();
    refreshLivePreview();
    Q_EMIT textBoxPropertiesChanged(presentedBox);
    Q_EMIT textBoxEditingChanged(true);
}

bool EditController::styleUnchanged(int page) const
{
    TextBoxProperties box = currentBox;
    box.bounds = presentedBox.bounds;
    return !activeEditMovedByUser && page == activeEditPresentedPage
        && currentEditorFontFamily == activeEditPresentedFamily
        && currentEditorBold       == activeEditPresentedBold
        && currentEditorItalic     == activeEditPresentedItalic
        && currentEditorUnderline  == activeEditPresentedUnderline
        && qFuzzyCompare(currentEditorFontSizePt + 1.0, activeEditPresentedFontSizePt + 1.0)
        && currentEditorColor      == activeEditPresentedColor
        && box == presentedBox;
}

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
                             currentEditorBold, currentEditorItalic,
                             currentEditorUnderline);

    if (!o.isSessionEdit) fitEditHeight(o);

    sampleEditColors(o);
    if (!o.isSessionEdit) fitEditWidth(o);

    presentEditor(o);
}

EditSession::Edit EditController::makeSessionEdit(int page, const QRectF &bounds,
                                                const QRectF &sourceRect,
                                                const QString &text) const
{
    EditSession::Edit e;
    e.page        = page;
    e.pdfBounds   = bounds;
    e.sourceRect  = activeEditNeedsBlank ? sourceRect : QRectF();
    e.newText     = text;
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
    e.underline   = currentEditorUnderline;
    e.fontChanged = editorFontChangedByUser;
    e.sizeChanged = editorSizeChangedByUser;
    e.formField   = activeEditFieldName;
    if (activeEditNeedsBlank) e.eraseRects = activeEditEraseRects;
    if (activeEditNeedsBlank && styleUnchanged(page)) e.originalLines = activeEditOriginalLines;
    e.box         = currentBox;
    e.box.bounds  = bounds;
    return e;
}
#endif

void EditController::commit(const QString &newText)
{
#ifdef HAVE_PDF_RENDERING
    if (activeEditPage < 0) return;

    const int    page       = activeEditPage;
    const int    srcPage    = activeEditSourcePage >= 0 ? activeEditSourcePage
                                                          : activeEditPage;
    const QRectF bounds     = activeEditBounds;
    const QRectF origBounds = activeEditOriginalBounds;
    activeEditPage       = -1;
    activeEditSourcePage = -1;

    m_frame->hide();
    Q_EMIT livePreviewChanged(-1, {});
    Q_EMIT textBoxEditingChanged(false);

    const QString trimNew = newText.trimmed();

    if (activeEditInPlace) {

        const double tol = qMax(1.0, 2.0 / PdfRenderer::screenScale(m_zoom->zoom()));
        const auto nearly = [tol](const QRectF &a, const QRectF &b) {
            return std::abs(a.left()   - b.left())   < tol
                && std::abs(a.top()    - b.top())    < tol
                && std::abs(a.width()  - b.width())  < tol
                && std::abs(a.height() - b.height()) < tol;
        };
        const bool textUntouched = newText.isNull()
            ? activeEditOriginalPlain.trimmed().isEmpty()
            : m_frame->plainText() == activeEditOriginalPlain;
        const bool untouched =
               page == activeEditPresentedPage
            && textUntouched
            && (!activeEditMovedByUser || nearly(bounds, activeEditPresentedBounds))
            && currentEditorFontFamily == activeEditPresentedFamily
            && currentEditorBold       == activeEditPresentedBold
            && currentEditorItalic     == activeEditPresentedItalic
            && currentEditorUnderline  == activeEditPresentedUnderline
            && qFuzzyCompare(currentEditorFontSizePt + 1.0,
                             activeEditPresentedFontSizePt + 1.0)
            && currentEditorColor      == activeEditPresentedColor;
        TextBoxProperties box = textBoxProperties();
        box.bounds = presentedBox.bounds;
        const bool boxUntouched = box == presentedBox;
        if (untouched && boxUntouched) {
            activeEditInPlace = false;
            activeEditFieldName.clear();
            m_session->restoreSuspended();
            lastCommittedPage = -1;
            Q_EMIT pageNeedsRerender(srcPage);
            if (page != srcPage) Q_EMIT pageNeedsRerender(page);
            return;
        }
    }
    activeEditInPlace = false;

    const auto snapBefore = undoSnapBefore;

    m_session->clearSuspended();

    m_session->removeEdit(srcPage, origBounds);
    if (page != srcPage || !bounds.intersects(origBounds))
        m_session->removeEdit(page, bounds);

    const bool needBlank = activeEditNeedsBlank;
    const QRectF eraseAt = activeEditEraseBounds.isNull() ? origBounds
                                                          : activeEditEraseBounds;
    if (needBlank) {
        EditSession::Edit blank = makeSessionEdit(srcPage, eraseAt, eraseAt,
                                                  QString());
        blank.eraseRects = activeEditEraseRects;
        m_session->addEdit(std::move(blank));
    }

    if (!trimNew.isEmpty())
        m_session->addEdit(makeSessionEdit(page, bounds, eraseAt, newText));
    activeEditFieldName.clear();

    const auto snapAfter = m_session->snapshotEdits();
    if (snapAfter != snapBefore) {

        m_undo->push(new EditUndoCmd(m_session, this, srcPage, page,
                                          snapBefore, snapAfter));

        Q_EMIT changeRecorded({ trimNew.isEmpty() ? DocumentHistory::Kind::TextRemoved
                                         : DocumentHistory::Kind::TextEdited,
                       page });
    }

    lastCommittedPage       = srcPage;
    lastCommittedOrigBounds = origBounds;

    Q_EMIT pageNeedsRerender(srcPage);
    if (page != srcPage)
        Q_EMIT pageNeedsRerender(page);
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
    Q_EMIT livePreviewChanged(-1, {});
    Q_EMIT textBoxEditingChanged(false);
    if (page >= 0)
        Q_EMIT pageNeedsRerender(page);
    if (srcPage >= 0 && srcPage != page)
        Q_EMIT pageNeedsRerender(srcPage);
#else
    activeEditPage = -1;
#endif
}

void EditController::createTextFrame(const QRect &viewportDragRect)
{
#ifdef HAVE_PDF_RENDERING

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
    activeEditPdfText       = QString();
    activeEditInPlace       = false;
    activeEditNeedsBlank    = false;
    activeEditEraseRects.clear();
    activeEditFieldName.clear();
    undoSnapBefore          = m_session->snapshotEdits();

    currentEditorColor = QColor(0x11, 0x11, 0x11);
    currentBgColor     = Qt::white;

    currentEditorFontFamily.clear();
    currentEditorBold       = false;
    currentEditorItalic     = false;
    currentEditorUnderline  = false;
    editorFontChangedByUser = false;
    editorSizeChangedByUser = false;
    currentEditorFontSizePt   = 12.0;
    currentEditorRenderSizePt = 12.0;

    activeEditHasOrigin     = false;
    activeEditOriginOffset  = QPointF();
    activeEditLineSpacingPt = 0.0;
    currentBox = defaultBox;
    currentBox.bounds = activeEditBounds;
    Q_EMIT fontSizeChanged(qRound(currentEditorFontSizePt));
    Q_EMIT fontChanged(QStringLiteral("Helvetica"), false, false, false);

    m_hover->hide();
    const qreal fontSize = qMax(1.0, currentEditorRenderSizePt * scale);
    m_frame->setDecorations(true);
    m_frame->setGlyphsVisible(false);
    m_frame->setGrowHorizontal(false);
    m_frame->setTextAnchor(false, QPointF());
    m_frame->setLineSpacingPt(activeEditLineSpacingPt);
    activeEditPdfPt = activeEditBounds.topLeft();
    refreshAdvanceMeasure();
    m_frame->setBoxProperties(currentBox, scale);
    m_frame->setPageRect(pageLbl->geometry());
    m_frame->setForbiddenZones({});
    m_frame->resetCommitGuard();
    m_frame->present(QString(),
                     QRectF(activeEditBounds.topLeft() * scale + QPointF(pageLbl->pos()),
                            activeEditBounds.size() * scale),
                     fontSize, currentEditorColor);
    presentedBox = textBoxProperties();
    Q_EMIT textBoxPropertiesChanged(presentedBox);
    Q_EMIT textBoxEditingChanged(true);
#else
    Q_UNUSED(viewportDragRect)
#endif
}
