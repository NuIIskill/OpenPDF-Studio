#include "engine/edit/EditSession.hpp"

#include <QPainter>
#include <QFont>
#include <QDebug>
#include <climits>

// ── Mutation ──────────────────────────────────────────────────────────────────

void EditSession::addEdit(int page, const QRectF &pdfBounds, const QString &newText,
                          double fontSizePt, const QColor &color, const QRectF &sourceRect,
                          const QColor &bgColor)
{
    Edit e;
    e.page       = page;
    e.pdfBounds  = pdfBounds;
    e.sourceRect = sourceRect;
    e.newText    = newText;
    e.fontSizePt = fontSizePt;
    e.textColor  = color;
    e.bgColor    = bgColor;
    m_edits.append(std::move(e));
}

void EditSession::removeEdit(int page, const QRectF &pdfBounds)
{
    m_edits.removeIf([&](const Edit &e) {
        return e.page == page && e.pdfBounds == pdfBounds;
    });
}

void EditSession::removeAllAt(int page, const QRectF &pdfBounds)
{
    m_edits.removeIf([&](const Edit &e) {
        return e.page == page && e.pdfBounds.intersects(pdfBounds);
    });
}

void EditSession::suspendEditsAt(int page, const QRectF &pdfBounds)
{
    m_suspendedEdits.clear();

    // Suspend ONLY edits whose pdfBounds exactly matches the clicked area.
    //
    // Previous companion-based logic (suspend everything linked via sourceRect)
    // caused "text jumping back": when text was moved P1→P2 (overlapping), clicking
    // on text(P2) also suspended blank(P1) via sourceRect, removing it from m_edits
    // and letting P1's original PDF content bleed through.
    //
    // The blank(P1) must persist in m_edits for as long as P1 is meant to be erased.
    // It is only suspended when the user directly clicks on P1's area — which isBlankAt
    // already prevents for move-source areas that have no companion text.
    QList<Edit> remaining;
    for (const Edit &e : m_edits) {
        if (e.page == page && e.pdfBounds == pdfBounds)
            m_suspendedEdits.append(e);
        else
            remaining.append(e);
    }
    m_edits = remaining;
}

bool EditSession::isBlankAt(int page, const QPointF &pdfPt) const
{
    // Point containment instead of exact rect equality: after a move, a fresh
    // click at the source position re-detects the native text with bounds that
    // never exactly match the stored blank (region-model expansion, clamping).
    // Clicks on neighbouring text stay unaffected — their click point lies
    // outside the blank.
    for (const auto &blank : m_edits) {
        if (blank.page != page || !blank.newText.isNull()
                || !blank.pdfBounds.contains(pdfPt))
            continue;
        // In-place edits pair blank+text at the same pdfBounds — that spot has
        // content and must open an editor, not be silently swallowed.
        bool hasCompanion = false;
        for (const auto &t : m_edits)
            if (t.page == page && !t.newText.isEmpty()
                    && t.pdfBounds == blank.pdfBounds) {
                hasCompanion = true;
                break;
            }
        if (!hasCompanion) return true;
    }
    return false;
}

bool EditSession::isBlankCovering(int page, const QRectF &bounds) const
{
    if (bounds.isEmpty()) return false;
    const double blockArea = bounds.width() * bounds.height();
    for (const auto &blank : m_edits) {
        if (blank.page != page || !blank.newText.isNull()) continue;
        const QRectF inter = blank.pdfBounds.intersected(bounds);
        if (inter.isEmpty()) continue;
        bool hasCompanion = false;
        for (const auto &t : m_edits)
            if (t.page == page && !t.newText.isEmpty()
                    && t.pdfBounds == blank.pdfBounds) {
                hasCompanion = true;
                break;
            }
        if (hasCompanion) continue;
        // Base the ratio on the SMALLER of the two areas: a text lookup that
        // merged the erased content into a larger block (neighbouring cell,
        // fuzzy snap) still counts as "covering" when the blank itself is
        // mostly inside the block.
        const double blankArea = blank.pdfBounds.width() * blank.pdfBounds.height();
        const double base      = qMax(1.0, qMin(blockArea, blankArea));
        if (inter.width() * inter.height() >= base * 0.5)
            return true;
    }
    return false;
}

QList<QRectF> EditSession::blankRegions(int page) const
{
    // Companion-less blanks = intentionally emptied areas. Text lookup must
    // treat them as if the text were gone — otherwise a click near them can
    // resurrect the invisible original.
    QList<QRectF> out;
    for (const auto &blank : m_edits) {
        if (blank.page != page || !blank.newText.isNull()) continue;
        bool hasCompanion = false;
        for (const auto &t : m_edits)
            if (t.page == page && !t.newText.isEmpty()
                    && t.pdfBounds == blank.pdfBounds) {
                hasCompanion = true;
                break;
            }
        if (!hasCompanion) out.append(blank.pdfBounds);
    }
    return out;
}

void EditSession::clearSuspended()
{
    m_suspendedEdits.clear();
}

void EditSession::restoreSuspended()
{
    // Re-insert suspended edits at the front so their original relative order
    // (blank before text) is preserved in applyToImage.
    m_edits = m_suspendedEdits + m_edits;
    m_suspendedEdits.clear();
}

void EditSession::clear()
{
    m_edits.clear();
    if (!m_imageEdits.isEmpty()) { m_imageEdits.clear(); ++m_imageRevision; }
}

// ── Image-edit CRUD ───────────────────────────────────────────────────────────

void EditSession::addImageEdit(int page, const QRectF &pdfBounds, const QImage &image)
{
    m_imageEdits.append({ page, pdfBounds, image });
    ++m_imageRevision;
}

void EditSession::removeImageEdit(int page, const QRectF &pdfBounds)
{
    m_imageEdits.removeIf([&](const ImageEdit &ie) {
        return ie.page == page && ie.pdfBounds == pdfBounds;
    });
    ++m_imageRevision;
}

bool EditSession::hasImageEditsOnPage(int page) const
{
    for (const auto &ie : m_imageEdits)
        if (ie.page == page) return true;
    return false;
}

void EditSession::clearImageEdits()
{
    if (m_imageEdits.isEmpty()) return;
    m_imageEdits.clear();
    ++m_imageRevision;
}

// ── Queries ───────────────────────────────────────────────────────────────────

bool EditSession::hasEditsOnPage(int page) const
{
    for (const auto &e : m_edits)
        if (e.page == page) return true;
    return false;
}

QString EditSession::editTextAt(int page, const QRectF &pdfBounds) const
{
    // Iterate in reverse so the most-recently-added (topmost) text edit wins.
    // Skip blank edits (null newText) — they don't carry display text and would
    // make in-place edits (blank+text at same pdfBounds) show native text instead
    // of the session text.
    for (int i = m_edits.size() - 1; i >= 0; --i) {
        const auto &e = m_edits[i];
        if (e.page == page && !e.newText.isNull() && e.pdfBounds.intersects(pdfBounds))
            return e.newText;
    }
    return QString(); // null = no existing edit
}

QColor EditSession::editColorAt(int page, const QRectF &pdfBounds) const
{
    for (int i = m_edits.size() - 1; i >= 0; --i) {
        const auto &e = m_edits[i];
        if (e.page == page && !e.newText.isNull() && e.pdfBounds.intersects(pdfBounds))
            return e.textColor;
    }
    return QColor(); // invalid = no stored color
}

bool EditSession::findEditAt(int page, const QPointF &pdfPt, Edit *out) const
{
    // Iterate in reverse so the topmost (most recently drawn) session edit wins
    // when multiple overlapping edits contain the click point.
    // Skip blank edits — they are erase-only and should not open an editor.
    for (int i = m_edits.size() - 1; i >= 0; --i) {
        const auto &e = m_edits[i];
        if (e.page == page && !e.newText.isNull() && e.pdfBounds.contains(pdfPt)) {
            if (out) *out = e;
            return true;
        }
    }
    return false;
}

// ── Live-view raster rendering (QImage overlay) ───────────────────────────────

void EditSession::applyToImage(int page, QImage &img, qreal scale) const
{
    if (img.isNull()) return;
    const bool hasText   = hasEditsOnPage(page);
    const bool hasImages = hasImageEditsOnPage(page);
    if (!hasText && !hasImages) return;

    QPainter p(&img);
    if (hasText) {
        // Two-pass: all blanks before all text draws. Blanks reconstruct the
        // background from the freshly rendered page pixels around them.
        for (const auto &e : m_edits)
            if (e.page == page && e.newText.isNull())
                paintBlankEdit(p, img, e, scale);
        for (const auto &e : m_edits)
            if (e.page == page && !e.newText.isEmpty())
                paintTextEdit(p, e, scale);
    }
    if (hasImages) {
        for (const ImageEdit &ie : m_imageEdits) {
            if (ie.page != page || ie.image.isNull()) continue;
            const QRect dst(
                qRound(ie.pdfBounds.left()   * scale),
                qRound(ie.pdfBounds.top()    * scale),
                qRound(ie.pdfBounds.width()  * scale),
                qRound(ie.pdfBounds.height() * scale));
            p.drawImage(dst, ie.image);
        }
    }
}

// Qt PDF renders pages onto a TRANSPARENT background — the "paper" is
// rgba(0,0,0,0), which reads as pitch black if alpha is ignored. Every
// sampled pixel must be composited over white (the paper color) first.
static inline QRgb overWhite(QRgb c)
{
    const int a = qAlpha(c);
    if (a == 255) return c;
    return qRgb((qRed(c)   * a + 255 * (255 - a)) / 255,
                (qGreen(c) * a + 255 * (255 - a)) / 255,
                (qBlue(c)  * a + 255 * (255 - a)) / 255);
}

void EditSession::paintBackgroundPatch(QPainter &p, const QImage &img,
                                       const QRect &rectPx)
{
    paintBackgroundPatch(p, img, QList<QRect>{ rectPx });
}

void EditSession::paintBackgroundPatch(QPainter &p, const QImage &img,
                                       const QList<QRect> &rectsPx)
{
    if (rectsPx.isEmpty()) return;
    const int W = img.width();
    const int H = img.height();

    int left = INT_MAX, right = INT_MIN, bTop = INT_MAX, bBot = INT_MIN;
    for (const QRect &r : rectsPx) {
        left  = qMin(left,  r.left());
        right = qMax(right, r.right());
        bTop  = qMin(bTop,  r.top());
        bBot  = qMax(bBot,  r.bottom());
    }
    left  = qMax(left, 0);
    right = qMin(right, W - 1);

    const auto closeRgb = [](QRgb a, QRgb b) {
        return qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b))
             + qAbs(qBlue(a) - qBlue(b)) < 36;
    };

    // BAND color: text often sits in a colored stripe exactly as tall as the
    // block (table header rows!). Above/below sampling then sees only the
    // page outside the stripe and paints the wrong color into it. Sample
    // LEFT and RIGHT of the whole block at its mid height — if both sides
    // agree, that is the true background behind the text.
    QRgb bandColor = 0;
    bool hasBand   = false;
    if (bTop <= bBot) {
        const int midY = qBound(0, (bTop + bBot) / 2, H - 1);
        QRgb l[2], r[2]; int nL = 0, nR = 0;
        for (const int dx : { 3, 8 }) {
            const int xl = left - dx, xr = right + dx;
            if (xl >= 0 && nL < 2) l[nL++] = overWhite(img.pixel(xl, midY));
            if (xr <  W && nR < 2) r[nR++] = overWhite(img.pixel(xr, midY));
        }
        if (nL == 2 && nR == 2 && closeRgb(l[0], l[1]) && closeRgb(r[0], r[1])
                && closeRgb(l[0], r[0])) {
            bandColor = l[0];
            hasBand   = true;
        }
    }

    for (int x = left; x <= right; ++x) {
        // Column span across the WHOLE block: topmost..bottommost covering
        // rect. Sampling between tightly stacked lines would read the
        // neighbouring line's glyphs and smear them into the fill, so the
        // samples sit strictly outside the block for this column.
        int top = INT_MAX, bot = INT_MIN;
        for (const QRect &r : rectsPx)
            if (x >= r.left() && x <= r.right()) {
                top = qMin(top, r.top());
                bot = qMax(bot, r.bottom());
            }
        if (top > bot) continue;
        top = qMax(top, 0);
        bot = qMin(bot, H - 1);
        if (top > bot) continue;

        QRgb above[2]; int nA = 0;
        QRgb below[2]; int nB = 0;
        for (const int dy : { 2, 6 }) {
            const int ya = top - dy;
            const int yb = bot + dy;
            if (ya >= 0 && nA < 2) above[nA++] = overWhite(img.pixel(x, ya));
            if (yb <  H && nB < 2) below[nB++] = overWhite(img.pixel(x, yb));
        }

        const int spanH = bot - top + 1;

        // Colored band behind the text → the band color wins; the vertical
        // neighbours are outside the stripe and would punch wrong-colored
        // holes into it.
        if (hasBand) {
            p.fillRect(x, top, 1, spanH, QColor::fromRgb(bandColor));
            continue;
        }

        QRgb cand[4]; int n = 0;
        for (int i = 0; i < nA; ++i) cand[n++] = above[i];
        for (int i = 0; i < nB; ++i) cand[n++] = below[i];
        if (n == 0) {
            p.fillRect(x, top, 1, spanH, Qt::white);
            continue;
        }

        const auto close = closeRgb;

        QRgb best = cand[0]; int bestScore = -1;
        for (int i = 0; i < n; ++i) {
            int score = 0;
            for (int j = 0; j < n; ++j)
                if (close(cand[i], cand[j])) ++score;
            if (score > bestScore) { bestScore = score; best = cand[i]; }
        }

        if (bestScore >= 2 || n == 1) {
            // Clear majority (or only one sample) — solid column fill.
            p.fillRect(x, top, 1, spanH, QColor::fromRgb(best));
        } else {
            // Above and below disagree — the span probably crosses a
            // horizontal background boundary. Blend vertically instead of
            // hard-guessing.
            const QRgb ct = nA > 0 ? above[0] : best;
            const QRgb cb = nB > 0 ? below[0] : best;
            QLinearGradient g(QPointF(x, top), QPointF(x, bot + 1));
            g.setColorAt(0.0, QColor::fromRgb(ct));
            g.setColorAt(1.0, QColor::fromRgb(cb));
            p.fillRect(QRect(x, top, 1, spanH), g);
        }
    }
}

void EditSession::paintBlankEdit(QPainter &p, const QImage &img, const Edit &e,
                                 qreal scale)
{
    // Erase per glyph line rect when available — a whole-bounds erase would
    // wipe graphics (chart bars, images) sharing the area with the text.
    // All rects are passed as ONE block so the reconstruction samples outside
    // the block instead of between its lines.
    const QList<QRectF> areas = e.eraseRects.isEmpty()
                                    ? QList<QRectF>{ e.pdfBounds }
                                    : e.eraseRects;
    // Pad in PDF points, scaled to pixels: glyph antialiasing bleeds ~1-2 pt
    // beyond the word boxes. A fixed 1 px pad left visible text traces at
    // higher render scales (300 dpi save!).
    const qreal pad = qMax(1.0, 2.5 * scale);
    QList<QRect> rects;
    rects.reserve(areas.size());
    for (const QRectF &a : areas) {
        const QRectF px(a.topLeft() * scale, a.size() * scale);
        rects.append(px.adjusted(-pad, -pad, pad, pad).toAlignedRect());
    }
    if (!img.isNull()) {
        paintBackgroundPatch(p, img, rects);
    } else {
        for (const QRect &r : rects)
            p.fillRect(r, e.bgColor.isValid() ? e.bgColor : Qt::white);
    }
}

void EditSession::paintTextEdit(QPainter &p, const Edit &e, qreal scale)
{
    const QRectF px(e.pdfBounds.topLeft() * scale, e.pdfBounds.size() * scale);
    // No white fill here — the companion blank edit handles erasure.
    // Text edits are drawn as overlays so that a text box placed near (or on top
    // of) existing content does not silently destroy it.

    // renderSizePt is fitted to the ink of the text being replaced; fontSizePt
    // is the file's own number, which in a substituted family draws too big or
    // too small. Prefer the fitted one whenever it exists.
    const double sizePt = e.renderSizePt > 0.0 ? e.renderSizePt : e.fontSizePt;
    int pixelSize;
    if (sizePt > 0.0) {
        pixelSize = qMax(6, qRound(sizePt * scale));
    } else {
        const int lineCount = qMax(1, e.newText.count(u'\n') + 1);
        pixelSize = qMax(8, qRound(px.height() / lineCount / 0.72));
    }
    // Use the edit's stored family/style so the live view matches what the
    // save path writes (original font kept, or user-chosen family).
    QFont f(e.fontFamily.isEmpty() ? QStringLiteral("Helvetica") : e.fontFamily);
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(pixelSize);
    f.setBold(e.bold);
    f.setItalic(e.italic);
    p.setFont(f);
    p.setPen(e.textColor.isValid() ? e.textColor : QColor(0x11, 0x11, 0x11));

    // Anchor on the original text's own starting point instead of the box:
    // laid out from the box top, the first baseline lands wherever the
    // substituted face happens to put it — several points off the line it
    // replaces.
    const QFontMetricsF fm(f);
    constexpr int kFlags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;
    QRectF target = px;
    if (e.hasTextOrigin) {
        const QPointF origin = (e.pdfBounds.topLeft() + e.textOriginOffset) * scale;
        target.translate(origin.x() - px.left(),
                         origin.y() - (px.top() + fm.ascent()));
    }
    // drawText clips to its rectangle, and the box is only ever as tall as the
    // ORIGINAL line — so descenders (and any line the anchor shifted upward)
    // get their bottoms sliced off. Wrapping must stay put, so only the height
    // grows; the width, which decides where lines break, does not.
    const auto laidOutHeight = [&](const QString &s) {
        return fm.boundingRect(QRectF(0, 0, target.width(), 1e6), kFlags, s).height();
    };
    const auto drawBlock = [&](const QString &s, qreal top) {
        QRectF r = target;
        r.moveTop(top);
        r.setHeight(qMax(target.height(), laidOutHeight(s) + fm.descent()));
        p.drawText(r.toRect(), kFlags, s);
    };

    const double stepPt = e.lineSpacingPt;
    if (stepPt <= 0.0 || !e.newText.contains(u'\n')) {
        drawBlock(e.newText, target.top());
        return;
    }
    // Multi-line block with a known spacing: place each line on the baseline it
    // had in the document instead of letting the font's tighter default stack
    // them. A line that grew long enough to wrap takes the room it needs, and
    // the ones below move down with it rather than being written over.
    const qreal step = stepPt * scale;
    qreal top = target.top();
    for (const QString &line : e.newText.split(u'\n')) {
        drawBlock(line, top);
        top += qMax(step, laidOutHeight(line));
    }
}
