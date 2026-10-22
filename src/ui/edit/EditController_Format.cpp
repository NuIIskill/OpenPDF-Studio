#include "ui/edit/EditController.hpp"

#include "ui/view/PageCanvas.hpp"

#include <algorithm>

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentSource.hpp"
#  include "engine/document/PdfBackend.hpp"
#  include "engine/render/PdfRenderer.hpp"
#  include "ui/edit/TextBoxFrame.hpp"
#  include "ui/view/ZoomController.hpp"

#  include <QLabel>
#  include <cmath>
#endif

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

bool EditController::openedAll(const std::function<bool(const TextLayout::Appearance &)> &has,
                               bool boxHad) const
{
    // The bar shows the character at the caret, so what is picked there goes
    // for the whole box, unless every character had it when the edit was
    // opened; then the box goes back to how it was. Without the look of each
    // character the box's own value stands for all of them.
    if (activeEditPage < 0 || m_opened.isEmpty()) return boxHad;
    return std::all_of(m_opened.cbegin(), m_opened.cend(), has);
}

void EditController::setFontFamily(const QString &family)
{
#ifdef HAVE_PDF_RENDERING
    if (family.isEmpty()) return;
    if (openedAll([&family](const TextLayout::Appearance &a) { return a.family == family; },
                  family == m_presented.family)) {
        currentEditorFontFamily   = m_presented.family;
        editorFamilyChangedByUser = m_presented.familyChanged;
    } else {
        if (family == currentEditorFontFamily && editorFamilyChangedByUser) return;
        currentEditorFontFamily   = family;
        editorFamilyChangedByUser = true;
        activeEditLaid            = false;
    }
    refreshFontLive();
#else
    Q_UNUSED(family)
#endif
}

void EditController::setBold(bool on)
{
#ifdef HAVE_PDF_RENDERING
    if (openedAll([on](const TextLayout::Appearance &a) { return a.bold == on; },
                  on == m_presented.bold)) {
        currentEditorBold       = m_presented.bold;
        editorBoldChangedByUser = m_presented.boldChanged;
    } else {
        if (on == currentEditorBold && editorBoldChangedByUser) return;
        currentEditorBold       = on;
        editorBoldChangedByUser = true;
        activeEditLaid          = false;
    }
    refreshFontLive();
#else
    Q_UNUSED(on)
#endif
}

void EditController::setItalic(bool on)
{
#ifdef HAVE_PDF_RENDERING
    if (openedAll([on](const TextLayout::Appearance &a) { return a.italic == on; },
                  on == m_presented.italic)) {
        currentEditorItalic       = m_presented.italic;
        editorItalicChangedByUser = m_presented.italicChanged;
    } else {
        if (on == currentEditorItalic && editorItalicChangedByUser) return;
        currentEditorItalic       = on;
        editorItalicChangedByUser = true;
        activeEditLaid            = false;
    }
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

void EditController::setFontSize(double ptSize)
{
#ifdef HAVE_PDF_RENDERING
    if (ptSize < 4.0 || ptSize > 400.0) return;
    if (activeEditPage < 0 || !m_frame->isVisible()) {
        currentEditorFontSizePt = ptSize;
        editorSizeChangedByUser = true;
        return;
    }
    // The size is picked for the character the bar shows, and the rest of the
    // box keeps its proportion to it. Where the box's size replaces every
    // character's size instead of scaling it, a second step lands on it.
    for (int step = 0; step < 2; ++step) {
        const double shown = m_shown.isValid() ? m_shown.sizePt : currentEditorFontSizePt;
        if (shown <= 0.0 || std::abs(shown - ptSize) < 0.005) break;
        double boxPt = currentEditorFontSizePt * ptSize / shown;
        if (std::abs(boxPt - m_presented.sizePt) < 0.005) boxPt = m_presented.sizePt;
        resizeText(boxPt);
        if (!m_shown.isValid()) break;
    }
#else
    Q_UNUSED(ptSize)
#endif
}

#ifdef HAVE_PDF_RENDERING
void EditController::resizeText(double ptSize)
{
    const qreal scale = PdfRenderer::screenScale(m_zoom->zoom());

    if (currentEditorFontSizePt > 0 && !qFuzzyCompare(ptSize, currentEditorFontSizePt)) {
        const qreal ratio = ptSize / currentEditorFontSizePt;
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
        // The first baseline keeps its place relative to the box, not its
        // distance: larger text would rise out of the box's top.
        if (activeEditHasOrigin) {
            activeEditOriginOffset *= ratio;
            m_frame->setTextAnchor(true, activeEditOriginOffset);
        }
    }

    currentEditorFontSizePt = ptSize;
    editorSizeChangedByUser = true;
    activeEditLaid          = false;
    refreshAdvanceMeasure();
    const QLabel *lbl = m_canvas->pageLabel(activeEditPage);
    if (lbl) {
        m_frame->setPageRect(lbl->geometry(), m_src->renderer()->pageSizePts(activeEditPage));
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
}
#endif

void EditController::setTextColor(const QColor &color)
{
#ifdef HAVE_PDF_RENDERING
    if (!color.isValid()) return;
    if (openedAll([&color](const TextLayout::Appearance &a) { return a.color == color; },
                  color == m_presented.color)) {
        currentEditorColor       = m_presented.color;
        editorColorChangedByUser = m_presented.colorChanged;
    } else {
        currentEditorColor       = color;
        editorColorChangedByUser = true;
    }
    if (activeEditPage >= 0 && m_frame->isVisible()) {
        m_frame->setTextColor(currentEditorColor);
        refreshAdvanceMeasure();
        refreshLivePreview();
    }
#else
    Q_UNUSED(color)
#endif
}

void EditController::showLook()
{
#ifdef HAVE_PDF_RENDERING
    if (activeEditPage < 0) return;
    // The character clicked or at the caret; the box's own style where the
    // editor knows no style per character.
    m_shown = m_frame->look();
    const TextLayout::Appearance shown = m_shown.isValid()
        ? m_shown
        : TextLayout::Appearance{ currentEditorFontFamily, currentEditorFontSizePt,
                                  currentEditorBold, currentEditorItalic, currentEditorColor };
    Q_EMIT fontSizeChanged(shown.sizePt);
    Q_EMIT fontChanged(shown.family.isEmpty() ? QStringLiteral("Helvetica") : shown.family,
                       shown.bold, shown.italic, currentEditorUnderline);
    Q_EMIT textColorChanged(shown.color);
#endif
}

void EditController::emitLineSpacing()
{
    const double sizePt = qMax(1.0, currentEditorFontSizePt);
    Q_EMIT lineSpacingChanged(currentBox.lineSpacingMultiplier > 0.0
                                  ? currentBox.lineSpacingMultiplier
                                  : TextLayout::lineStep(sizePt, activeEditLineSpacingPt) / sizePt);
}
