#include "ui/edit/EditController.hpp"

#include "ui/view/PageCanvas.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentSource.hpp"
#  include "engine/edit/EditCodec.hpp"
#  include "engine/document/PdfBackend.hpp"
#  include "engine/render/PdfRenderer.hpp"
#  include "engine/edit/ContentMap.hpp"
#  include "engine/edit/SourcedText.hpp"
#  include "ui/edit/TextBoxFrame.hpp"
#  include "ui/view/HoverHighlight.hpp"
#  include "ui/view/ZoomController.hpp"

#  include <QApplication>
#  include <QMimeData>
#  include <QRawFont>
#  include <QLabel>
#  include <QStringList>
#  include <QUndoStack>
#  include <QWidget>
#  include <cmath>
#  include <type_traits>
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

#ifdef HAVE_PDF_RENDERING
QByteArray EditController::encodedEdits() const
{
    return m_session ? EditCodec::encode(m_session->edits()) : QByteArray();
}

void EditController::restoreEncodedEdits(const QByteArray &data)
{
    QList<EditSession::Edit> edits;
    if (!m_session || !EditCodec::decode(data, &edits) || edits == m_session->edits()) return;
    QList<int> pages;
    for (const auto *list : { &m_session->edits(), &std::as_const(edits) })
        for (const EditSession::Edit &e : *list)
            for (int page : { e.page, e.sourcePage })
                if (page >= 0 && !pages.contains(page)) pages.append(page);
    m_session->restoreEdits(std::move(edits));
    for (int page : std::as_const(pages)) Q_EMIT pageNeedsRerender(page);
}
#endif

EditController::Look EditController::currentLook() const
{
    Look look;
    look.page      = activeEditPage;
    look.family    = currentEditorFontFamily;
    look.bold      = currentEditorBold;
    look.italic    = currentEditorItalic;
    look.underline = currentEditorUnderline;
    look.sizePt    = currentEditorFontSizePt;
    look.color     = currentEditorColor;
    look.bounds    = activeEditBounds;
    look.box       = textBoxProperties();
    look.familyChanged = editorFamilyChangedByUser;
    look.boldChanged   = editorBoldChangedByUser;
    look.italicChanged = editorItalicChangedByUser;
    look.colorChanged  = editorColorChangedByUser;
#ifdef HAVE_PDF_RENDERING
    look.marks         = m_frame->charMarks();
#endif
    return look;
}

bool EditController::Look::sameStyle(const Look &o) const
{
    TextBoxProperties withBounds = box;
    withBounds.bounds = o.box.bounds;
    return page == o.page && family == o.family && bold == o.bold && italic == o.italic
        && underline == o.underline && qFuzzyCompare(sizePt + 1.0, o.sizePt + 1.0)
        && color == o.color && withBounds == o.box && familyChanged == o.familyChanged
        && boldChanged == o.boldChanged && italicChanged == o.italicChanged
        && colorChanged == o.colorChanged;
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
    const bool eigene = editorFamilyChangedByUser || editorBoldChangedByUser
                     || editorItalicChangedByUser || activeEditPdfText.isEmpty();
    m_frame->setStandardFace(eigene && !(backend && backend->canEmbedFont(
        currentEditorFontFamily, currentEditorBold, currentEditorItalic)));
    if (!backend) { m_frame->setMetricsSource({}); return; }
    m_frame->setMetricsSource([this, backend](const QString &text) {
        if (activeEditPage < 0) return TextLayout::Metrics();
        const QRectF eraseAt = activeEditEraseBounds.isNull() ? activeEditOriginalBounds
                                                              : activeEditEraseBounds;
        m_metricsPage   = activeEditPage;
        m_metricsBounds = activeEditBounds;
        return backend->editMetrics(makeSessionEdit(activeEditPage, activeEditBounds,
                                                    eraseAt, text));
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
    // A framed edit shows as what closing it gives: the paragraph or box it
    // goes back into.
    const std::optional<EditSession::Edit> merged =
        m_hasUnit && !activeEditMovedByUser && currentLook().sameStyle(m_presented)
            ? mergedFrameEdit(text) : std::nullopt;
    if (merged) {
        EditSession::Edit blank;
        blank.page       = activeEditSourcePage >= 0 ? activeEditSourcePage : activeEditPage;
        blank.sourcePage = blank.page;
        blank.pdfBounds  = merged->sourceRect;
        blank.eraseRects = activeEditEraseRects;
        for (const QRectF &r : m_unit.eraseRects)
            if (!blank.eraseRects.contains(r)) blank.eraseRects.append(r);
        edits.append(blank);
        if (!merged->newText.trimmed().isEmpty()) edits.append(*merged);
        Q_EMIT livePreviewChanged(activeEditPage, edits);
        return;
    }
    if (activeEditFieldName.isEmpty() && !text.isEmpty()) {
        const QRectF eraseAt = activeEditEraseBounds.isNull()
                                   ? activeEditOriginalBounds : activeEditEraseBounds;
        edits.append(makeSessionEdit(activeEditPage, activeEditBounds, eraseAt, text));
    }
    edits += m_rest;
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
    m_frame->setPageRect(lbl->geometry(), m_src->renderer()->pageSizePts(activeEditPage));
    const QRectF cb(activeEditBounds.topLeft() * scale + QPointF(lbl->pos()),
                    activeEditBounds.size() * scale);
    m_frame->repositionForZoom(
        cb, qMax(1.0, currentEditorFontSizePt * scale),
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
    EditOpener::clampToPage(m_src, page, r);
#else
    Q_UNUSED(page) Q_UNUSED(r)
#endif
}

#ifdef HAVE_PDF_RENDERING

void EditController::presentEditor(const EditTarget &t)
{

    activeEditOriginOffset = activeEditHasOrigin
                                   ? t.textOrigin - activeEditBounds.topLeft()
                                   : QPointF();

    m_hover->hide();

    if (t.isSessionEdit) {
        activeEditEraseRects = t.sessionEdit.eraseRects;
    } else {
        activeEditEraseRects.clear();
        if (t.scanned) {
            activeEditEraseRects = t.scanRects;
        } else if (!t.framedLines.isEmpty()) {
            // A frame takes out exactly the text it took, not all lines in its bounds.
            for (const TextLayout::OriginalLine &line : t.framedLines)
                activeEditEraseRects.append(line.rect);
        } else if (activeEditNeedsBlank) {
            if (auto *backend = m_src->backend())
                activeEditEraseRects = backend->glyphRects(
                    t.block.page, activeEditOriginalBounds, t.erasedZones);
        }
        activeEditEraseBounds = activeEditOriginalBounds;
    }
    activeEditOriginalLines.clear();
    if (t.laid) {
        activeEditOriginalLines = t.framedLines;
    } else if (t.isSessionEdit) {
        activeEditOriginalLines = t.sessionEdit.originalLines;
    } else if (t.scanned && !t.framedLines.isEmpty()) {
        // A frame on a scan goes back into its paragraph; nothing of the
        // invisible text layer is kept, all of it is set anew.
        activeEditOriginalLines = t.framedLines;
        for (TextLayout::OriginalLine &line : activeEditOriginalLines)
            line.objectStart = QList<bool>(line.text.size(), false);
    } else if (activeEditNeedsBlank && activeEditFieldName.isEmpty() && !t.scanned) {
        if (auto *backend = m_src->backend()) {
            const QList<TextLayout::OriginalLine> lines = !t.framedLines.isEmpty()
                ? t.framedLines
                : backend->originalLines(t.block.page, activeEditOriginalBounds, t.erasedZones);
            if (!lines.isEmpty() && TextLayout::originalText(lines) == t.displayText)
                activeEditOriginalLines = lines;
        }
        // The layout keeps the original glyphs where they are only while the
        // box puts the first of them exactly on its own origin.
        if (!activeEditOriginalLines.isEmpty() && !activeEditOriginalLines.first().charX.isEmpty()) {
            const TextLayout::OriginalLine &first = activeEditOriginalLines.first();
            activeEditHasOrigin    = true;
            activeEditOriginOffset = QPointF(first.charX.first(), first.baseline)
                                   - activeEditBounds.topLeft();
        }
    }

    const QRectF canvasBounds(
        activeEditBounds.topLeft() * t.scale + QPointF(t.label->pos()),
        activeEditBounds.size() * t.scale);
    const qreal fontSize = qMax(1.0, currentEditorFontSizePt * t.scale);

    m_frame->setDecorations(true);
    m_frame->setGlyphsVisible(!activeEditFieldName.isEmpty());
    m_frame->setLineSpacingPt(activeEditLineSpacingPt);
    activeEditPdfPt = t.pdfPt;
    refreshAdvanceMeasure();
    m_frame->setTextAnchor(activeEditHasOrigin, activeEditOriginOffset);
    m_frame->setForbiddenZones({});
    m_frame->setPageRect(t.label->geometry(), m_src->renderer()->pageSizePts(t.editPage));
    m_frame->setGrowHorizontal(!t.isSessionEdit
                               || (!t.sessionEdit.originalLines.isEmpty()
                                   && !t.sessionEdit.newText.contains(QChar(TextLayout::kSoftBreak))));
    currentBox.bounds = activeEditBounds;
    m_frame->setBoxProperties(currentBox, t.scale);
    m_frame->resetCommitGuard();
    undoSnapBefore = m_session->snapshotEdits();
    m_session->suspendEditsAt(t.block.page, t.block.pdfBounds,
                              t.blankPage, activeEditEraseBounds);
    if (activeEditNeedsBlank)
        Q_EMIT pageNeedsBlank(activeEditPage, activeEditEraseBounds);
    else
        Q_EMIT pageNeedsRerender(activeEditPage);
    m_frame->setDocumentKey(documentKey());
    m_frame->present(t.displayText, canvasBounds, fontSize,
                           currentEditorColor, currentEditorFontFamily,
                           currentEditorBold, currentEditorItalic,
                           currentEditorUnderline);
    // Pasted characters keep their source and marked ones their mark; the
    // editor's text has no soft breaks.
    const auto unbroken = [&t](const auto &list) {
        std::remove_cvref_t<decltype(list)> out;
        for (qsizetype i = 0; i < list.size(); ++i)
            if (i >= t.sessionEdit.newText.size()
                    || t.sessionEdit.newText.at(i) != QChar(TextLayout::kSoftBreak))
                out.append(list.at(i));
        return out;
    };
    if (t.laid) {
        m_frame->setCharSources(t.framedSources);
        if (!t.framedMarks.isEmpty()) m_frame->setCharMarks(t.framedMarks);
    } else if (t.isSessionEdit) {
        if (!t.sessionEdit.sources.isEmpty()) m_frame->setCharSources(unbroken(t.sessionEdit.sources));
        if (!t.sessionEdit.marks.isEmpty()) m_frame->setCharMarks(unbroken(t.sessionEdit.marks));
    }

    activeEditOriginalText        = m_frame->currentText();
    activeEditOriginalPlain       = m_frame->plainText();
    activeEditMovedByUser         = false;
    refreshLivePreview();
    activeEditInPlace             = true;
    m_presented                   = currentLook();
    refreshAdvanceMeasure();
    refreshLivePreview();
    Q_EMIT textBoxPropertiesChanged(m_presented.box);
    emitLineSpacing();
    Q_EMIT textBoxEditingChanged(true);
}

void EditController::handleClick(const QPoint &canvasPos)
{
    openAt(canvasPos, canvasPos, false);
}

QString EditController::documentKey() const
{
    return SourcedText::documentKey(m_src);
}

void EditController::handleFrame(const QPoint &from, const QPoint &to)
{
    // A frame is drawn on purpose, also over the text just closed.
    lastCommittedPage = -1;
    openAt(from, to, true);
}

QList<QRect> EditController::framedText(const QPoint &from, const QPoint &to)
{
    QList<QRect> out;
    int page = -1;
    const QList<TextLayout::OriginalLine> lines = m_opener.linesInFrame(from, to, nullptr, &page);
    const QLabel *label = page >= 0 ? m_canvas->pageLabel(page) : nullptr;
    if (!label) return out;
    const qreal scale = PdfRenderer::screenScale(m_zoom->zoom());
    for (const TextLayout::OriginalLine &line : lines)
        out.append(QRectF(line.rect.topLeft() * scale + QPointF(label->pos()),
                          line.rect.size() * scale).toAlignedRect());
    return out;
}

void EditController::openAt(const QPoint &from, const QPoint &to, bool framed)
{
    EditTarget t;
    t.color = currentEditorColor;
    if (!m_opener.open(from, to, framed, lastCommittedPage, lastCommittedOrigBounds, t)) {
        if (m_opener.ocrMissing()) Q_EMIT noticeWanted(EditNotice::Kind::Ocr);
        return;
    }
    openTarget(t);
    if (!framed && m_frame->isVisible()) m_frame->showLookAt(from);
    showLook();
    // Text that cannot be set exactly as it looks is said to be so.
    if (!t.isSessionEdit && activeEditPage >= 0) {
        if (t.scanned) Q_EMIT noticeWanted(EditNotice::Kind::Scan);
        else if (!t.exactFont) Q_EMIT noticeWanted(EditNotice::Kind::Font);
    }
}

void EditController::openTarget(const EditTarget &t)
{
    activeEditPage           = t.editPage;
    activeEditSourcePage     = t.sourcePage;
    activeEditBounds         = t.bounds;
    activeEditOriginalBounds = t.originalBounds;
    activeEditNeedsBlank     = t.needsBlank;
    activeEditEraseBounds    = t.eraseBounds;
    currentBox               = t.box;
    activeEditFieldName      = t.fieldName;
    activeEditOriginalText   = t.originalText;
    activeEditPdfText        = t.pdfText;
    activeEditLineSpacingPt  = t.lineSpacingPt;
    activeEditHasOrigin      = t.hasOrigin;
    currentEditorFontSizePt   = t.fontSizePt;
    editorSizeChangedByUser   = t.sizeChangedByUser;
    editorColorChangedByUser  = t.colorChangedByUser;
    currentEditorFontFamily   = t.fontFamily;
    currentEditorBold         = t.bold;
    currentEditorItalic       = t.italic;
    currentEditorUnderline    = t.underline;
    editorFamilyChangedByUser = t.familyChangedByUser;
    editorBoldChangedByUser   = t.boldChangedByUser;
    editorItalicChangedByUser = t.italicChangedByUser;
    activeEditBaseSizePt      = t.baseSizePt;
    currentEditorColor        = t.color;
    currentBgColor            = t.bgColor;
    activeEditLaid            = t.laid || (t.isSessionEdit && t.sessionEdit.laid);
    activeEditScanned         = t.scanned;
    m_rest                    = t.rest;
    m_hasUnit                 = t.hasUnit;
    m_unit                    = t.unit;
    m_unitSources             = t.unitSources;
    m_unitMarks               = t.unitMarks;

    presentEditor(t);
    m_opened = m_frame->appearances();
    if (t.selStart >= 0 && m_frame->isVisible() && m_frame->plainText() == t.selectionIn)
        m_frame->selectRange(t.selStart, t.selEnd);
}

EditSession::Edit EditController::makeSessionEdit(int page, const QRectF &bounds,
                                                const QRectF &sourceRect,
                                                const QString &text) const
{
    EditSession::Edit e;
    e.page        = page;
    e.sourcePage  = activeEditSourcePage >= 0 ? activeEditSourcePage : page;
    e.pdfBounds   = bounds;
    e.sourceRect  = activeEditNeedsBlank ? sourceRect : QRectF();
    e.newText     = text;
    e.fontSizePt  = currentEditorFontSizePt;
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
    e.familyChanged = editorFamilyChangedByUser;
    e.boldChanged   = editorBoldChangedByUser;
    e.italicChanged = editorItalicChangedByUser;
    e.baseSizePt    = activeEditBaseSizePt;
    e.sizeChanged = editorSizeChangedByUser;
    e.colorChanged = editorColorChangedByUser;
    e.formField   = activeEditFieldName;
    e.laid        = activeEditLaid;
    e.scanned     = activeEditScanned;
    if (activeEditNeedsBlank) {
        e.eraseRects = activeEditEraseRects;
        if (activeEditSourcePage < 0 || activeEditSourcePage == page)
            e.originalLines = activeEditOriginalLines;
    }
    // Lines where the text was set belong to the page it is on.
    if (activeEditLaid) e.originalLines = activeEditOriginalLines;
    e.sources     = TextLayout::alignSources(text, m_frame->charSources());
    e.marks       = TextLayout::alignMarks(text, m_frame->charMarks());
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
    // A framed edit goes back into the paragraph or box it was cut from.
    const std::optional<EditSession::Edit> merged =
        m_hasUnit && !activeEditMovedByUser && currentLook().sameStyle(m_presented)
            ? mergedFrameEdit(newText) : std::nullopt;
    // A framed part moved away leaves its line closed up behind it.
    const std::optional<EditSession::Edit> left =
        m_hasUnit && activeEditMovedByUser ? mergedFrameEdit(QString()) : std::nullopt;
    m_hasUnit = false;
    activeEditPage       = -1;
    activeEditSourcePage = -1;

    hideFrame();
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
        // Spaces taken out are a change too.
        const bool textUntouched = newText.isNull()
            ? activeEditOriginalPlain.isEmpty()
            : m_frame->plainText() == activeEditOriginalPlain;
        Look now = currentLook();
        now.page = page;
        const bool untouched = textUntouched && now.sameStyle(m_presented)
            && now.marks == m_presented.marks
            && (!activeEditMovedByUser || nearly(bounds, m_presented.bounds));
        if (untouched) {
            activeEditInPlace = false;
            activeEditFieldName.clear();
            m_rest.clear();
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
    if (merged) {
        if (needBlank) {
            EditSession::Edit blank = makeSessionEdit(srcPage, merged->sourceRect,
                                                      merged->sourceRect, QString());
            blank.sourcePage = srcPage;
            blank.eraseRects = activeEditEraseRects;
            for (const QRectF &r : m_unit.eraseRects)
                if (!blank.eraseRects.contains(r)) blank.eraseRects.append(r);
            m_session->addEdit(std::move(blank));
        }
        if (!merged->newText.trimmed().isEmpty()) m_session->addEdit(*merged);
        m_rest.clear();
    } else if (left) {
        EditSession::Edit blank = makeSessionEdit(srcPage, left->sourceRect, left->sourceRect, QString());
        blank.sourcePage = srcPage;
        blank.eraseRects = activeEditEraseRects;
        for (const QRectF &r : m_unit.eraseRects)
            if (!blank.eraseRects.contains(r)) blank.eraseRects.append(r);
        m_session->addEdit(std::move(blank));
        if (!left->newText.trimmed().isEmpty()) m_session->addEdit(*left);
        m_rest.clear();
    } else if (needBlank) {
        EditSession::Edit blank = makeSessionEdit(srcPage, eraseAt, eraseAt,
                                                  QString());
        blank.sourcePage = srcPage;
        blank.eraseRects = activeEditEraseRects;
        m_session->addEdit(std::move(blank));
    }

    if (!merged && !trimNew.isEmpty()) {
        EditSession::Edit text = makeSessionEdit(page, bounds, eraseAt, newText);
        // The original lines describe glyphs of the source page; on another
        // page the same spot holds other text.
        text.sourcePage = srcPage;
        if (page != srcPage && !text.laid) text.originalLines.clear();
        m_session->addEdit(std::move(text));
    }
    for (EditSession::Edit &rest : m_rest) m_session->addEdit(std::move(rest));
    m_rest.clear();
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
    m_rest.clear();
    m_hasUnit = false;
    m_session->restoreSuspended();
    hideFrame();
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

#ifdef HAVE_PDF_RENDERING
void EditController::hideFrame()
{
    // Keys the box held go back to the page view (the viewport's focus proxy), not to a panel.
    if (m_frame->isAncestorOf(QApplication::focusWidget()))
        m_canvas->canvasWidget()->parentWidget()->setFocus(Qt::OtherFocusReason);
    m_frame->hide();
}
#endif

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
    editorFamilyChangedByUser = false;
    editorBoldChangedByUser   = false;
    editorItalicChangedByUser = false;
    activeEditBaseSizePt      = 12.0;
    editorSizeChangedByUser  = false;
    editorColorChangedByUser = false;
    currentEditorFontSizePt  = 12.0;

    activeEditHasOrigin     = false;
    activeEditOriginOffset  = QPointF();
    activeEditLineSpacingPt = 0.0;
    activeEditLaid          = false;
    activeEditScanned       = false;
    m_rest.clear();
    m_hasUnit = false;
    currentBox = defaultBox;
    currentBox.bounds = activeEditBounds;
    m_opened.clear();

    m_hover->hide();
    const qreal fontSize = qMax(1.0, currentEditorFontSizePt * scale);
    m_frame->setDecorations(true);
    m_frame->setGlyphsVisible(false);
    m_frame->setGrowHorizontal(false);
    m_frame->setTextAnchor(false, QPointF());
    m_frame->setLineSpacingPt(activeEditLineSpacingPt);
    activeEditPdfPt = activeEditBounds.topLeft();
    refreshAdvanceMeasure();
    m_frame->setBoxProperties(currentBox, scale);
    m_frame->setPageRect(pageLbl->geometry(), m_src->renderer()->pageSizePts(pageIdx));
    m_frame->setForbiddenZones({});
    m_frame->resetCommitGuard();
    m_frame->setDocumentKey(documentKey());
    m_frame->present(QString(),
                     QRectF(activeEditBounds.topLeft() * scale + QPointF(pageLbl->pos()),
                            activeEditBounds.size() * scale),
                     fontSize, currentEditorColor);
    m_presented = currentLook();
    showLook();
    Q_EMIT textBoxPropertiesChanged(m_presented.box);
    emitLineSpacing();
    Q_EMIT textBoxEditingChanged(true);
#else
    Q_UNUSED(viewportDragRect)
#endif
}
