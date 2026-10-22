#include "ui/edit/EditOpener.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/document/DocumentSource.hpp"
#include "ui/edit/EditOpen.hpp"
#include "ui/view/PageCanvas.hpp"
#include "ui/view/ZoomController.hpp"

#include <QApplication>
#include <QFontMetricsF>
#include <QLabel>
#include <QStringList>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

bool EditOpener::resolveEditTarget(const QPoint &canvasPos, EditOpen &o, int &lastCommittedPage,
                                   const QRectF &lastCommittedOrigBounds)
{
    auto [pageIdx, pageLbl] = m_canvas->pageAtCanvasPos(canvasPos);
    if (pageIdx < 0) return false;

    o.page     = pageIdx;
    o.label    = pageLbl;
    o.scale    = PdfRenderer::screenScale(m_zoom->zoom());
    o.pdfPt    = QPointF(canvasPos - pageLbl->pos()) / o.scale;
    o.clickPt  = o.pdfPt;
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

    // A click beside document text edited before opens it, as it did before
    // the edit, unless other text of the document lies nearer.
    if (!o.isSessionEdit && !o.contentItem.isFormField()) {
        const EditSession::Edit *near = nullptr;
        double nearest = 40.0;
        for (const EditSession::Edit &e : m_session->edits()) {
            if (e.page != pageIdx || e.newText.isEmpty() || !e.formField.isEmpty()
                    || e.sourceRect.isNull())
                continue;
            const double d = distanceTo(e.pdfBounds, o.pdfPt);
            if (d <= nearest) { nearest = d; near = &e; }
        }
        if (near && (!o.block.isValid() || nearest < distanceTo(o.block.pdfBounds, o.pdfPt))) {
            o.sessionEdit   = *near;
            o.block         = TextBlock{ pageIdx, near->pdfBounds, near->newText };
            o.isSessionEdit = true;
            o.contentItem   = ContentItem{};
        }
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
        if (ocr.isValid()) {
            o.block   = TextBlock{ pageIdx, ocr.pdfBounds, ocr.text };
            o.scanned = true;
        }
    }

    if (!o.block.isValid()) return false;

    if (o.block.page == lastCommittedPage &&
        o.block.pdfBounds.intersects(lastCommittedOrigBounds)) {
        lastCommittedPage = -1;
        return false;
    }
    lastCommittedPage = -1;

    // The text left beside a removed part still opens, from its gap too; the
    // text found never holds removed characters.
    const bool onText = o.block.pdfBounds.adjusted(-1, -1, 1, 1).contains(o.pdfPt);
    if (!o.isSessionEdit && ((m_session->isBlankAt(o.block.page, o.pdfPt) && !onText)
                             || (o.contentItem.isFormField()
                                 && m_session->isBlankCovering(o.block.page, o.block.pdfBounds))))
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

void EditOpener::applyEditTargetBounds(EditOpen &o)
{
    o.editPage           = o.block.page;
    o.sourcePage     = o.block.page;
    o.bounds         = o.block.pdfBounds;
    clampToPage(m_src, o.block.page, o.bounds);
    o.originalBounds = o.bounds;
    o.needsBlank     = true;
    o.eraseBounds    = o.bounds;
    if (o.isSessionEdit) {
        o.needsBlank  = !o.sessionEdit.eraseRects.isEmpty();
        o.eraseBounds = o.sessionEdit.sourceRect.isNull()
                                    ? o.bounds : o.sessionEdit.sourceRect;
        const int source = o.sessionEdit.sourcePage >= 0 ? o.sessionEdit.sourcePage : o.block.page;
        for (const EditSession::Edit &e : m_session->edits()) {
            if (!e.newText.isNull() || e.pdfBounds != o.eraseBounds
                    || e.formField != o.sessionEdit.formField)
                continue;
            if (o.blankPage < 0 || e.page == source) o.blankPage = e.page;
        }
        if (o.blankPage >= 0) o.sourcePage = o.blankPage;
    }
    o.box = o.isSessionEdit ? o.sessionEdit.box : TextBoxProperties{};
    o.fieldName.clear();

    if (!o.isSessionEdit && o.contentItem.isValid()) {
        if (!o.contentItem.bounds.isEmpty()) {
            const double fs = o.contentItem.fontSizePt > 0.0
                                  ? o.contentItem.fontSizePt : 12.0;
            const bool extractorUsable = o.block.pdfBounds.height() > 0.5
                                      && o.block.pdfBounds.height() <= fs * 2.5;
            if (!extractorUsable || o.contentItem.isFormField()) {
                o.bounds         = o.contentItem.bounds;
                o.originalBounds = o.contentItem.bounds;
                clampToPage(m_src, o.block.page, o.bounds);
            }
        }
        if (o.contentItem.type == ContentItem::Type::Paragraph
                || (o.contentItem.type == ContentItem::Type::TableCell
                    && o.contentItem.text.contains(u'\n'))) {

            o.paragraphBounds = true;
            TextBlock para;
            if (auto *backend = m_src->backend())
                para = backend->blockInRect(o.block.page, o.contentItem.bounds,
                                            o.erasedZones);

            const bool paraSane = para.isValid()
                               && para.pdfBounds.height() < o.pageSize.height() * 0.5;
            if (paraSane && !para.text.isEmpty()) {
                o.displayText    = para.text;
                o.bounds         = para.pdfBounds;
                o.originalBounds = para.pdfBounds;
                clampToPage(m_src, o.block.page, o.bounds);
            } else {
                o.paragraphBounds = false;
            }
        }
        if (o.contentItem.isFormField()) {
            o.fieldName = o.contentItem.fieldName;
            o.displayText         = o.contentItem.text;

            o.needsBlank = !o.contentItem.text.isEmpty();
        }
    }
    o.originalText = o.displayText;
    o.pdfText      = o.isSessionEdit ? o.sessionEdit.originalText
                                               : o.displayText;
    if (o.isSessionEdit)
        o.fieldName = o.sessionEdit.formField;
    // Text whose only visible form is a scan's pixels: what shows is measured there.
    if (o.isSessionEdit)
        o.scanned = o.sessionEdit.scanned;
    else if (!o.scanned && o.fieldName.isEmpty() && m_src->backend())
        o.scanned = m_src->backend()->hiddenText(o.block.page, o.originalBounds);
}

void EditOpener::chooseEditorFont(EditOpen &o)
{

    bool sizeIsExact = false;
    o.sizeChangedByUser = false;
    if (o.isSessionEdit && o.sessionEdit.fontSizePt > 0.0) {
        o.fontSizePt = qMax(4.0, o.sessionEdit.fontSizePt);
        o.sizeChangedByUser = o.sessionEdit.sizeChanged;
        sizeIsExact = true;
    } else {
        const double detectedPt = (!o.scanned && o.contentItem.isValid()
                                   && o.contentItem.fontSizePt > 0.0)
                                      ? o.contentItem.fontSizePt : 0.0;
        if (detectedPt > 0.0 && detectedPt <= 144.0) {
            const double polyEst   = o.block.pdfBounds.height() / 0.72;
            const bool   plausible = (detectedPt <= polyEst * 4.0)
                                   || (o.block.pdfBounds.height() >= 20.0);
            if (plausible) {

                o.fontSizePt = qMax(4.0, detectedPt);
                sizeIsExact = o.contentItem.fontSizeExact;
            } else {
                o.fontSizePt = qMax(4.0, qMin(polyEst, 28.0));
            }
        } else {
            const int    lineCount = qMax(1, o.displayText.count(u'\n') + 1);
            const double lineH     = qMin(o.bounds.height() / lineCount,
                                          20.0);
            o.fontSizePt = qMax(4.0, lineH / 0.72);
        }
    }

    if (!o.isSessionEdit && !o.contentItem.isFormField()) {
        const double lineGlyphH = o.block.pdfBounds.height();
        if (lineGlyphH > 4.0 && lineGlyphH < 60.0
                && o.fontSizePt > lineGlyphH * 1.5) {
            o.fontSizePt = qMax(4.0, lineGlyphH * 1.05);
            sizeIsExact = false;
        }
    }

    if (o.isSessionEdit) {
        o.fontFamily = o.sessionEdit.fontFamily;
        o.bold       = o.sessionEdit.bold;
        o.italic     = o.sessionEdit.italic;
        o.underline  = o.sessionEdit.underline;
        o.familyChangedByUser = o.sessionEdit.familyChanged;
        o.boldChangedByUser   = o.sessionEdit.boldChanged;
        o.italicChangedByUser = o.sessionEdit.italicChanged;
    } else if (!o.scanned && !o.contentItem.fontFamily.isEmpty()) {
        o.fontFamily = o.contentItem.fontFamily;
        o.bold       = o.contentItem.bold;
        o.italic     = o.contentItem.italic;
        o.underline  = false;
    } else {
        o.fontFamily.clear();
        o.bold       = false;
        o.italic     = false;
        o.underline  = false;
    }

    if (!o.isSessionEdit && !o.scanned && !o.contentItem.isFormField()) {
        if (auto *backend = m_src->backend()) {
            const QString embedded =
                backend->embeddedFontFamily(o.block.page, o.pdfPt);
            if (!embedded.isEmpty()) o.fontFamily = embedded;
            o.exactFont = backend->exactFontAt(o.block.page, o.pdfPt);
        }
    }

    o.colorChangedByUser = o.isSessionEdit && o.sessionEdit.colorChanged;
    o.measurable = !sizeIsExact && !o.isSessionEdit && !o.contentItem.isFormField()
                && !o.displayText.isEmpty() && o.canSample();
    if (!o.measurable) return;

    // A scan is set in the face its pixels match, at the size its words are wide.
    ScanFont::Match &face = o.scanFace;
    face = {};
    if (o.scanned) matchScanFace(o);

    // A scan's text layer may lie a little beside its pixels.
    const InkMetrics::MeasuredInk origInk = o.measuredInk(o.scanned ? o.inkAround(o.bounds) : o.bounds);
    QFont probe(o.fontFamily.isEmpty()
                    ? QStringLiteral("Helvetica") : o.fontFamily);
    probe.setStyleHint(QFont::SansSerif);
    probe.setBold(o.bold);
    probe.setItalic(o.italic);
    o.probeInk = InkMetrics::fontInkPerPt(o.displayText, probe);
    if (!face.isValid() && origInk.height > 1.0 && o.probeInk.heightPerPt > 0.05) {

        const double fitted = origInk.height / o.probeInk.heightPerPt;
        o.fontSizePt = qBound(qMax(4.0, o.fontSizePt * 0.6), fitted,
                              qMax(5.0, o.fontSizePt * 1.5));
    }
    if (o.scanned && !face.isValid()) {
        // Bold strokes are wider than about a ninth of the size.
        const double stroke = o.strokeWidth(o.inkAround(o.block.pdfBounds));
        o.bold = stroke > 0.0 && stroke > o.fontSizePt * 0.115;
    }
}

void EditOpener::anchorEditOrigin(EditOpen &o)
{

    o.lineSpacingPt = o.isSessionEdit ? o.sessionEdit.lineSpacingPt
                                                : o.contentItem.lineSpacingPt;

    o.hasOrigin = false;
    const QList<ScanFont::Fit> &fits = o.scanFace.lines;
    if (!o.isSessionEdit && o.scanFace.isValid() && !fits.isEmpty() && fits.first().sizePt > 0.0) {
        // On a scan the pen starts where the matched face covered the first line.
        o.hasOrigin = true;
        o.textOrigin = fits.first().originPt;
        QList<double> steps;
        for (int i = 1; i < fits.size(); ++i)
            if (fits.at(i).sizePt > 0.0 && fits.at(i - 1).sizePt > 0.0)
                steps.append(fits.at(i).originPt.y() - fits.at(i - 1).originPt.y());
        std::sort(steps.begin(), steps.end());
        if (!steps.isEmpty()) o.lineSpacingPt = steps.at(steps.size() / 2);
    } else if (o.isSessionEdit) {
        o.hasOrigin = o.sessionEdit.hasTextOrigin;
        o.textOrigin = o.sessionEdit.pdfBounds.topLeft()
                     + o.sessionEdit.textOriginOffset;
    } else if (!o.scanned && !o.contentItem.textOrigin.isNull()
               && o.contentItem.textOrigin.y() >= o.bounds.top() - 1.0
               && o.contentItem.textOrigin.y()
                      <= o.bounds.top()
                             + o.fontSizePt * 1.6) {

        o.hasOrigin = true;
        o.textOrigin = o.contentItem.textOrigin;
    } else if (o.measurable && o.probeInk.risePerPt > 0.05) {
        const QRectF top = o.scanned ? o.inkAround(scanLines(o).first())
                                     : QRectF(o.bounds.left(), o.bounds.top(), o.bounds.width(),
                                              qMin(o.bounds.height(), o.fontSizePt * 1.5));
        const InkMetrics::MeasuredInk first = o.measuredInk(top);
        if (first.height > 1.0) {
            o.hasOrigin = true;
            o.textOrigin = QPointF(
                first.left - o.probeInk.bearingPerPt * o.fontSizePt,
                first.top  + o.probeInk.risePerPt    * o.fontSizePt);
        }
    }
}

void EditOpener::fitEditHeight(EditOpen &o)
{
    if (o.fontSizePt <= 0) return;

    QFont line(o.fontFamily.isEmpty()
                   ? QStringLiteral("Helvetica") : o.fontFamily);
    line.setStyleHint(QFont::SansSerif);
    line.setPixelSize(qMax(1, qRound(o.fontSizePt * o.scale)));
    line.setBold(o.bold);
    line.setItalic(o.italic);
    const QFontMetricsF fm(line);
    const double scale = qMax(0.01, o.scale);

    if (o.hasOrigin) {
        const double baseline = o.textOrigin.y();
        o.bounds.setTop(qMin(o.bounds.top(),
                                     baseline - fm.ascent() / scale));
        o.bounds.setBottom(qMax(o.bounds.bottom(),
                                        baseline + fm.descent() / scale));
        clampToPage(m_src, o.editPage, o.bounds);
    } else if (o.bounds.height() < fm.height() / scale) {
        o.bounds.setHeight(fm.height() / scale);
        clampToPage(m_src, o.editPage, o.bounds);
    }

    if (!o.paragraphBounds) {
        const double capH = o.fontSizePt * 5.0;
        if (o.bounds.height() > capH) {
            o.bounds.setHeight(capH);
            clampToPage(m_src, o.editPage, o.bounds);
        }
    }
    o.originalBounds = o.bounds;
}

void EditOpener::sampleEditColors(EditOpen &o)
{

    if (o.isSessionEdit && o.sessionEdit.textColor.isValid()) {
        o.color = o.sessionEdit.textColor;
    } else if (!o.isSessionEdit && !o.scanned && o.contentItem.isValid()
               && o.contentItem.textColor.isValid()) {
        o.color = o.contentItem.textColor;
    } else if (o.canSample()) {
        o.color = o.sampleTextColor(o.block.pdfBounds);
    }

    o.bgColor = Qt::white;
    if (o.isSessionEdit && o.sessionEdit.bgColor.isValid()) {
        o.bgColor = o.sessionEdit.bgColor;
        return;
    }
    QColor bg;
    if (!o.scanned && o.contentItem.isValid() && o.contentItem.bgColor.isValid()) {
        const QColor c   = o.contentItem.bgColor;
        const qreal  lum = 0.299 * c.redF() + 0.587 * c.greenF()
                         + 0.114 * c.blueF();
        if (lum >= 0.70) bg = c;
    }
    if (!bg.isValid() && o.canSample())
        bg = o.sampleBackgroundColor(o.bounds);
    if (bg.isValid()) o.bgColor = bg;
}

void EditOpener::fitEditWidth(EditOpen &o)
{

    if (o.fontSizePt <= 0 || o.displayText.isEmpty()) return;

    QFont measure(o.fontFamily.isEmpty()
                      ? QStringLiteral("Helvetica")
                      : o.fontFamily);
    measure.setStyleHint(QFont::SansSerif);

    measure.setPixelSize(qMax(1, qRound(o.fontSizePt)));
    measure.setBold(o.bold);
    measure.setItalic(o.italic);
    const QFontMetricsF fm(measure);
    qreal needW = 0.0;
    const QStringList lines = o.displayText.split(u'\n');
    for (const QString &ln : lines)
        needW = qMax(needW, fm.horizontalAdvance(ln));
    needW += 1.5;
    if (needW > o.bounds.width())
        o.bounds.setWidth(
            qMin(needW, o.pageSize.width() - o.bounds.left() - 2.0));
}


namespace {

// The characters of a line a frame takes: those whose middle lies inside it,
// spaces too.
std::pair<int, int> charsInFrame(const TextLayout::OriginalLine &line, const QRectF &frame)
{
    const int n = int(line.text.size());
    int first = -1, last = -1;
    for (int i = 0; i < n; ++i) {
        const double right = i + 1 < n ? line.charX.at(i + 1) : line.endX;
        const double middle = (line.charX.at(i) + right) / 2.0;
        // A row of spaces is read back as one wide space; a frame within it takes it.
        const bool within = line.text.at(i).isSpace() && frame.left() >= line.charX.at(i)
                         && frame.right() <= right;
        if (!within && (middle < frame.left() || middle > frame.right())) continue;
        if (first < 0) first = i;
        last = i;
    }
    return { first, last + 1 };
}

bool halfInFrame(const TextLayout::OriginalLine &line, const QRectF &frame)
{
    const double inside = std::min(line.rect.bottom(), frame.bottom())
                        - std::max(line.rect.top(), frame.top());
    return inside >= line.rect.height() * 0.5;
}

// Characters first to end of a line, each where it is.
TextLayout::OriginalLine sliceOf(const TextLayout::OriginalLine &line, int first, int end)
{
    const int n = int(line.text.size());
    TextLayout::OriginalLine part;
    part.text        = line.text.mid(first, end - first);
    part.charX       = line.charX.mid(first, end - first);
    part.objectStart = line.objectStart.mid(first, end - first);
    part.baseline    = line.baseline;
    part.endX        = end < n ? line.charX.at(end) : line.endX;
    part.rect = QRectF(part.charX.first(), line.rect.top(),
                       part.endX - part.charX.first(), line.rect.height());
    return part;
}

// What of one line a frame takes. Where it cuts a text object the part has no
// object start, so nothing of it is kept as an original object; the cut
// object is split when the text is taken out.
std::optional<TextLayout::OriginalLine> partInFrame(const TextLayout::OriginalLine &line,
                                                    const QRectF &frame)
{
    const int n = int(line.text.size());
    const auto [first, end] = charsInFrame(line, frame);
    if (first < 0) return std::nullopt;
    if (first == 0 && end == n) return line;
    TextLayout::OriginalLine part = sliceOf(line, first, end);
    if (!line.objectStart.value(first) || (end < n && !line.objectStart.value(end)))
        part.objectStart.fill(false);
    return part;
}

QList<TextLayout::Source> flatSources(const QList<QList<TextLayout::Source>> &lines)
{
    // Aligned with the lines joined by line breaks, which have no source.
    QList<TextLayout::Source> out;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (i > 0) out.append(TextLayout::Source{});
        out += lines.at(i);
    }
    return out;
}

}

QList<TextLayout::OriginalLine> EditOpener::linesInFrame(const QPoint &from, const QPoint &to,
                                                         QPoint *firstAt, int *pageOut, Cut *cut)
{
    // The frame decides what is edited, not the detection: the text of every
    // line lying at least half its height inside it. The page's lines are
    // read once per frame, not on every step of the drag.
    QList<TextLayout::OriginalLine> out;
    PdfBackend *backend = m_src->backend();
    auto [page, label] = m_canvas->pageAtCanvasPos((from + to) / 2);
    if (!backend || page < 0) return out;
    const qreal scale = PdfRenderer::screenScale(m_zoom->zoom());
    const QPointF origin(label->pos());
    const QRectF frame = QRectF((QPointF(from) - origin) / scale,
                                (QPointF(to) - origin) / scale).normalized();
    if (pageOut) *pageOut = page;

    // Text edited or moved before is taken from where it is set now.
    // The smallest box holding the frame's middle, else the one it covers most.
    const EditSession::Edit *edited = nullptr;
    double most = -1.0, smallest = -1.0;
    for (const EditSession::Edit &e : m_session->edits()) {
        if (e.page != page || e.newText.isEmpty() || !e.formField.isEmpty()) continue;
        const QRectF hit = e.pdfBounds.intersected(frame);
        const double area = hit.width() * hit.height();
        const double size = e.pdfBounds.width() * e.pdfBounds.height();
        if (e.pdfBounds.contains(frame.center())) {
            if (smallest < 0.0 || size < smallest) { smallest = size; edited = &e; }
            continue;
        }
        if (smallest >= 0.0 || area < frame.width() * frame.height() * 0.5) continue;
        if (area > most) { most = area; edited = &e; }
    }
    if (edited) {
        if (!m_editLines.valid || !(m_editLines.edit == *edited)) {
            m_editLines = { true, *edited, {}, {} };
            m_editLines.lines = backend->editLines(*edited, &m_editLines.sources);
        }
        Cut taken;
        taken.valid   = true;
        taken.edit    = *edited;
        taken.lines   = m_editLines.lines;
        taken.sources = m_editLines.sources;
        for (qsizetype k = 0; k < m_editLines.lines.size(); ++k) {
            const TextLayout::OriginalLine &line = m_editLines.lines.at(k);
            const QList<TextLayout::Source> sources = m_editLines.sources.value(k);
            const int n = int(line.text.size());
            const auto [first, end] = halfInFrame(line, frame) ? charsInFrame(line, frame)
                                                               : std::pair<int, int>(-1, 0);
            if (first < 0) {
                taken.rest.append(line);
                taken.restSources.append(sources);
                continue;
            }
            TextLayout::OriginalLine part = sliceOf(line, first, end);
            part.objectStart.fill(false);
            out.append(part);
            taken.takenSources.append(sources.mid(first, end - first));
            // What is left of the line stays where it is, a gap where the part was.
            TextLayout::OriginalLine left = first > 0 ? sliceOf(line, 0, first) : TextLayout::OriginalLine();
            TextLayout::OriginalLine right = end < n ? sliceOf(line, end, n) : TextLayout::OriginalLine();
            TextLayout::OriginalLine kept = left.text.isEmpty() ? right : left;
            if (!left.text.isEmpty() && !right.text.isEmpty()) {
                kept.text  += right.text;
                kept.charX += right.charX;
                kept.endX   = right.endX;
                kept.rect   = left.rect.united(right.rect);
            }
            kept.objectStart = QList<bool>(kept.text.size(), false);
            if (!kept.text.trimmed().isEmpty()) {
                taken.rest.append(kept);
                taken.restSources.append(sources.mid(0, first) + sources.mid(end));
            }
        }
        if (!out.isEmpty() && firstAt)
            *firstAt = (origin + edited->pdfBounds.center() * scale).toPoint();
        if (cut) *cut = taken;
        return out;
    }

    if (m_pageLines.page != page)
        m_pageLines = { page, backend->originalLines(
                                  page, QRectF(QPointF(), m_src->renderer()->pageSizePts(page)),
                                  m_session->blankRegions(page)) };
    for (const TextLayout::OriginalLine &line : std::as_const(m_pageLines.lines)) {
        if (line.text.isEmpty() || line.charX.size() != line.text.size()) continue;
        if (!halfInFrame(line, frame)) continue;
        if (const auto part = partInFrame(line, frame)) {
            out.append(*part);
            if (cut) cut->wholeLines.append(line);
        }
    }
    if (out.isEmpty()) {
        // A frame on the gap between two pieces of one line, text that far
        // apart being read as two, takes the gap as spaces.
        const TextLayout::OriginalLine *before = nullptr, *after = nullptr;
        for (const TextLayout::OriginalLine &line : std::as_const(m_pageLines.lines)) {
            if (line.text.isEmpty() || line.charX.size() != line.text.size()
                    || !halfInFrame(line, frame))
                continue;
            if (line.endX <= frame.center().x() && (!before || line.endX > before->endX))
                before = &line;
            if (line.charX.first() >= frame.center().x()
                    && (!after || line.charX.first() < after->charX.first()))
                after = &line;
        }
        if (before && after && std::abs(before->baseline - after->baseline) < 1.0
                && frame.left() < after->charX.first() && frame.right() > before->endX) {
            TextLayout::OriginalLine gap;
            gap.text        = QStringLiteral(" ");
            gap.charX       = { before->endX };
            gap.objectStart = { false };
            gap.baseline    = after->baseline;
            gap.endX        = after->charX.first();
            gap.rect        = QRectF(before->endX, after->rect.top(),
                                     gap.endX - before->endX, after->rect.height());
            out.append(gap);
            if (cut) cut->gapLines = { *before, *after };
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const TextLayout::OriginalLine &l, const TextLayout::OriginalLine &r) {
        if (std::abs(l.baseline - r.baseline) > 1.0) return l.baseline < r.baseline;
        return l.charX.first() < r.charX.first();
    });
    if (!out.isEmpty() && firstAt)
        *firstAt = (origin + out.first().rect.center() * scale).toPoint();
    return out;
}

bool EditOpener::scopeToFrame(EditOpen &o, const QList<TextLayout::OriginalLine> &lines)
{
    if (o.isSessionEdit || o.contentItem.isFormField()) return false;

    QRectF bounds;
    QStringList texts;
    for (const TextLayout::OriginalLine &line : lines) {
        bounds = bounds.isNull() ? line.rect : bounds.united(line.rect);
        texts << line.text;
    }
    const QString text = texts.join(QLatin1Char('\n'));
    o.block           = TextBlock{ o.page, bounds, text };
    o.displayText     = text;
    o.pdfPt           = lines.first().rect.center();
    o.paragraphBounds = lines.size() > 1;
    o.framedLines     = lines;
    o.selStart = o.selEnd = int(text.size());
    if (o.contentItem.isValid()) {
        // The frame's text, not the paragraph or cell the detection found there.
        o.contentItem.type       = ContentItem::Type::Text;
        o.contentItem.bounds     = bounds;
        o.contentItem.text       = text;
        o.contentItem.textOrigin = QPointF(lines.first().charX.first(), lines.first().baseline);
    }
    return true;
}

void EditOpener::scopeToCut(EditOpen &o, const QList<TextLayout::OriginalLine> &lines, const Cut &cut)
{
    // The box edited before is opened on the part framed, where it is set; the
    // rest of it becomes a box of its own, every character where it was and in
    // its style, and is saved with the edit.
    QRectF bounds;
    QStringList texts;
    for (const TextLayout::OriginalLine &line : lines) {
        bounds = bounds.isNull() ? line.rect : bounds.united(line.rect);
        texts << line.text;
    }
    const QString text = texts.join(QLatin1Char('\n'));
    o.framedLines   = lines;
    o.framedSources = flatSources(cut.takenSources);
    o.displayText   = o.originalText = o.pdfText = o.selectionIn = text;
    o.selStart = o.selEnd = int(text.size());
    o.bounds = o.originalBounds = bounds;
    clampToPage(m_src, o.editPage, o.bounds);
    o.textOrigin = QPointF(lines.first().charX.first(), lines.first().baseline);
    o.hasOrigin  = true;
    o.laid       = true;

    o.rest.clear();
    if (cut.rest.isEmpty()) return;
    EditSession::Edit rest = cut.edit;
    QStringList restTexts;
    QRectF restBounds;
    for (const TextLayout::OriginalLine &line : cut.rest) {
        restTexts << line.text;
        restBounds = restBounds.isNull() ? line.rect : restBounds.united(line.rect);
    }
    rest.newText          = restTexts.join(QLatin1Char('\n'));
    rest.originalText     = rest.newText;
    rest.originalLines    = cut.rest;
    rest.sources          = flatSources(cut.restSources);
    rest.pdfBounds        = restBounds;
    rest.box              = TextBoxProperties{};
    rest.box.bounds       = restBounds;
    rest.hasTextOrigin    = true;
    rest.textOriginOffset = QPointF(cut.rest.first().charX.first(), cut.rest.first().baseline)
                          - restBounds.topLeft();
    rest.sourcePage       = rest.page;
    rest.sourceRect       = QRectF();
    rest.eraseRects.clear();
    rest.laid             = true;
    o.rest = { rest };
}

bool EditOpener::open(const QPoint &from, const QPoint &to, bool framed, int &lastCommittedPage,
                      const QRectF &lastCommittedOrigBounds, EditTarget &target)
{
    EditOpen o;
    o.color = target.color;
    QPoint at = from;
    QList<TextLayout::OriginalLine> lines;
    Cut cut;
    if (framed) {
        lines = linesInFrame(from, to, &at, nullptr, &cut);
        forgetFrameLines();
        if (lines.isEmpty()) return false;
    }
    if (!resolveEditTarget(at, o, lastCommittedPage, lastCommittedOrigBounds)) return false;
    if (cut.valid && !(o.isSessionEdit && o.sessionEdit == cut.edit)) return false;
    EditOpen whole = o;
    const bool scoped = framed && !cut.valid && scopeToFrame(o, lines);
    if (scoped) o.selectionIn = o.displayText;

    applyEditTargetBounds(o);
    chooseEditorFont(o);
    o.baseSizePt = o.isSessionEdit && o.sessionEdit.baseSizePt > 0.0 ? o.sessionEdit.baseSizePt
                                                                      : o.fontSizePt;
    anchorEditOrigin(o);
    if (!o.isSessionEdit) fitEditHeight(o);
    sampleEditColors(o);
    if (!o.isSessionEdit) fitEditWidth(o);
    measureScan(o);
    if (cut.valid) scopeToCut(o, lines, cut);
    if (scoped) findUnit(whole, o, cut.gapLines, cut.wholeLines);
    if (cut.valid && !cut.lines.isEmpty()) {
        o.hasUnit            = true;
        o.unit               = cut.edit;
        o.unit.originalLines = cut.lines;
        o.unit.laid          = true;
        o.unitSources        = cut.sources;
    }

    target = o;
    return true;
}

void EditOpener::findUnit(EditOpen &whole, EditOpen &o, const QList<TextLayout::OriginalLine> &gapLines,
                          const QList<TextLayout::OriginalLine> &wholeLines)
{
    // What a click there opens, the paragraph or line, which the framed edit
    // goes back into when it is closed, so the text stays one piece.
    PdfBackend *backend = m_src->backend();
    if (!backend || whole.isSessionEdit || whole.contentItem.isFormField() || !whole.block.isValid())
        return;
    applyEditTargetBounds(whole);
    if (!whole.fieldName.isEmpty()) return;
    chooseEditorFont(whole);
    whole.baseSizePt = whole.fontSizePt;
    anchorEditOrigin(whole);
    fitEditHeight(whole);
    sampleEditColors(whole);
    // A gap framed between two pieces of a line goes back into those two; a
    // frame the paragraph does not hold whole goes back into its lines.
    QList<TextLayout::OriginalLine> lines = !gapLines.isEmpty() ? gapLines
        : backend->originalLines(whole.block.page, whole.originalBounds, whole.erasedZones);
    bool own = !gapLines.isEmpty();
    if (!own) {
        bool holds = !lines.isEmpty() && TextLayout::originalText(lines) == whole.displayText;
        for (const TextLayout::OriginalLine &w : wholeLines) {
            bool found = false;
            for (const TextLayout::OriginalLine &l : std::as_const(lines))
                found |= l.text == w.text && std::abs(l.baseline - w.baseline) < 0.5;
            holds &= found;
        }
        if (!holds) {
            if (wholeLines.isEmpty()) return;
            lines = wholeLines;
            own = true;
        }
    }

    // Each character's style, from the same characters the select tool reads.
    QHash<QPair<qint64, qint64>, TextLayout::Source> at;
    const PdfBackend::Selection page = backend->selectPage(whole.block.page, std::nullopt, std::nullopt);
    qsizetype k = 0;
    for (const TextLayout::OriginalLine &line : page.lines) {
        for (qsizetype i = 0; i < line.text.size() && i < line.charX.size(); ++i, ++k)
            at.insert({ qRound64(line.baseline * 10), qRound64(line.charX.at(i) * 100) }, page.sources.value(k));
        ++k;
    }
    // Without a style for every character the original characters give it.
    QList<QList<TextLayout::Source>> sources;
    for (const TextLayout::OriginalLine &line : lines) {
        QList<TextLayout::Source> styles;
        for (qsizetype i = 0; i < line.charX.size(); ++i) {
            const QPair<qint64, qint64> key(qRound64(line.baseline * 10), qRound64(line.charX.at(i) * 100));
            styles.append(at.value(key));
        }
        sources.append(styles);
    }

    EditSession::Edit &u = o.unit;
    u.page          = whole.editPage;
    u.sourcePage    = whole.editPage;
    u.pdfBounds     = whole.bounds;
    u.sourceRect    = whole.eraseBounds.isNull() ? whole.originalBounds : whole.eraseBounds;
    u.fontSizePt    = whole.fontSizePt;
    u.baseSizePt    = whole.baseSizePt;
    u.lineSpacingPt = whole.lineSpacingPt;
    u.originalText  = whole.pdfText;
    u.textColor     = whole.color;
    u.bgColor       = whole.bgColor;
    u.fontFamily    = whole.fontFamily;
    u.bold          = whole.bold;
    u.italic        = whole.italic;
    u.underline     = whole.underline;
    u.eraseRects    = backend->glyphRects(whole.block.page, whole.originalBounds, whole.erasedZones);
    if (own) {
        u.eraseRects.clear();
        u.pdfBounds = QRectF();
        for (const TextLayout::OriginalLine &line : std::as_const(lines)) {
            u.eraseRects.append(line.rect);
            u.pdfBounds = u.pdfBounds.isNull() ? line.rect : u.pdfBounds.united(line.rect);
        }
        u.sourceRect = u.pdfBounds;
    }
    // On a scan the unit covers the ink of its lines and is set anew, all of
    // it, in the face its pixels match: the text layer is invisible. Lines
    // set otherwise, a heading over its paragraph, cannot be one edit.
    if (whole.scanned) {
        for (const TextLayout::OriginalLine &line : std::as_const(lines)) {
            const ScanFont::Match m = whole.scanFont({ whole.scanLine(line.rect, line.text) }, {}, whole.fontFamily);
            if (m.isValid() && (std::abs(m.sizePt - whole.fontSizePt) > whole.fontSizePt * 0.04 || m.bold != whole.bold))
                return;
        }
        u.scanned = true;
        u.eraseRects.clear();
        for (TextLayout::OriginalLine &line : lines) {
            u.eraseRects.append(whole.inkAround(line.rect).adjusted(-0.75, -0.75, 0.75, 0.75));
            line.objectStart = QList<bool>(line.text.size(), false);
        }
        sources.clear();
    }
    // It starts where the matched face covered the pixels, not where the layer put its text.
    if (whole.scanned && whole.hasOrigin) {
        u.hasTextOrigin    = true;
        u.textOriginOffset = whole.textOrigin - u.pdfBounds.topLeft();
    }
    u.originalLines = lines;
    u.box           = whole.box;
    u.box.bounds    = u.pdfBounds;
    o.unitSources   = sources;
    o.hasUnit       = true;
}

void EditOpener::clampToPage(const DocumentSource *src, int page, QRectF &r)
{
    if (!src->renderer() || page < 0) return;
    const QSizeF ps = src->renderer()->pageSizePts(page);

    r = r.normalized();
    if (r.width()  > ps.width())  r.setWidth(ps.width());
    if (r.height() > ps.height()) r.setHeight(ps.height());

    if (r.left()   < 0)           r.moveLeft(0);
    if (r.top()    < 0)           r.moveTop(0);
    if (r.right()  > ps.width())  r.moveRight(ps.width());
    if (r.bottom() > ps.height()) r.moveBottom(ps.height());
}

#endif
