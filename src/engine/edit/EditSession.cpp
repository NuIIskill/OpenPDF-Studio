#include "engine/edit/EditSession.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QDebug>
#include <climits>

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

    for (const auto &blank : m_edits) {
        if (blank.page != page || !blank.newText.isNull()
                || !blank.pdfBounds.contains(pdfPt))
            continue;

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

        const double blankArea = blank.pdfBounds.width() * blank.pdfBounds.height();
        const double base      = qMax(1.0, qMin(blockArea, blankArea));
        if (inter.width() * inter.height() >= base * 0.5)
            return true;
    }
    return false;
}

QList<QRectF> EditSession::blankRegions(int page) const
{

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

    m_edits = m_suspendedEdits + m_edits;
    m_suspendedEdits.clear();
}

void EditSession::clear()
{
    m_edits.clear();
    if (!m_imageEdits.isEmpty()) { m_imageEdits.clear(); ++m_imageRevision; }
    m_drawStrokes.clear();
    m_linkEdits.clear();
    m_noteEdits.clear();
}

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

void EditSession::replaceDrawStrokes(QList<DrawStroke> strokes)
{
    if (m_drawStrokes == strokes) return;
    m_drawStrokes = std::move(strokes);
}

bool EditSession::hasDrawEditsOnPage(int page) const
{
    for (const DrawStroke &stroke : m_drawStrokes)
        if (stroke.page == page) return true;
    return false;
}

void EditSession::clearImageEdits()
{
    if (m_imageEdits.isEmpty()) return;
    m_imageEdits.clear();
    ++m_imageRevision;
}

void EditSession::replaceLinkEdits(QList<LinkEdit> edits)
{
    if (m_linkEdits == edits) return;
    m_linkEdits = std::move(edits);
}

bool EditSession::hasLinkEditsOnPage(int page) const
{
    for (const LinkEdit &edit : m_linkEdits)
        if (edit.page == page) return true;
    return false;
}

void EditSession::replaceNoteEdits(QList<NoteEdit> edits)
{
    if (m_noteEdits == edits) return;
    m_noteEdits = std::move(edits);
}

bool EditSession::hasNoteEditsOnPage(int page) const
{
    for (const NoteEdit &edit : m_noteEdits)
        if (edit.page == page) return true;
    return false;
}

bool EditSession::hasEditsOnPage(int page) const
{
    for (const auto &e : m_edits)
        if (e.page == page) return true;
    return false;
}

QString EditSession::editTextAt(int page, const QRectF &pdfBounds) const
{

    for (int i = m_edits.size() - 1; i >= 0; --i) {
        const auto &e = m_edits[i];
        if (e.page == page && !e.newText.isNull() && e.pdfBounds.intersects(pdfBounds))
            return e.newText;
    }
    return QString();
}

QColor EditSession::editColorAt(int page, const QRectF &pdfBounds) const
{
    for (int i = m_edits.size() - 1; i >= 0; --i) {
        const auto &e = m_edits[i];
        if (e.page == page && !e.newText.isNull() && e.pdfBounds.intersects(pdfBounds))
            return e.textColor;
    }
    return QColor();
}

bool EditSession::findEditAt(int page, const QPointF &pdfPt, Edit *out) const
{

    for (int i = m_edits.size() - 1; i >= 0; --i) {
        const auto &e = m_edits[i];
        if (e.page == page && !e.newText.isNull() && e.pdfBounds.contains(pdfPt)) {
            if (out) *out = e;
            return true;
        }
    }
    return false;
}

void EditSession::applyToImage(int page, QImage &img, qreal scale, Paint what) const
{
    if (img.isNull()) return;
    const bool fieldsOnly = what == Paint::FormFields;
    const auto wanted = [fieldsOnly](const Edit &e) {
        return !fieldsOnly || !e.formField.isEmpty();
    };
    bool hasText = false;
    for (const auto &e : m_edits)
        if (e.page == page && wanted(e)) { hasText = true; break; }
    const bool hasImages = !fieldsOnly && hasImageEditsOnPage(page);
    const bool hasDraw   = !fieldsOnly && hasDrawEditsOnPage(page);
    if (!hasText && !hasImages && !hasDraw) return;

    QPainter p(&img);
    if (hasText) {

        for (const auto &e : m_edits)
            if (e.page == page && e.newText.isNull() && wanted(e))
                paintBlankEdit(p, img, e, scale);
        for (const auto &e : m_edits)
            if (e.page == page && !e.newText.isEmpty() && wanted(e))
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
    if (hasDraw) {
        p.setRenderHint(QPainter::Antialiasing);
        for (const DrawStroke &stroke : m_drawStrokes) {
            if (stroke.page != page || stroke.points.isEmpty()) continue;
            QPen pen(stroke.color, qMax<qreal>(0.5, stroke.widthPt * scale),
                     Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(pen);
            if (stroke.points.size() == 1) {
                p.drawPoint(stroke.points.first() * scale);
                continue;
            }
            QPainterPath path(stroke.points.first() * scale);
            for (int i = 1; i < stroke.points.size(); ++i)
                path.lineTo(stroke.points.at(i) * scale);
            p.drawPath(path);
        }
    }
}

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

            p.fillRect(x, top, 1, spanH, QColor::fromRgb(best));
        } else {

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

    const QList<QRectF> areas = e.eraseRects.isEmpty()
                                    ? QList<QRectF>{ e.pdfBounds }
                                    : e.eraseRects;

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
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setOpacity(qBound(0.0, e.box.opacity, 1.0));
    if (!qFuzzyIsNull(e.box.rotationDeg)) {
        p.translate(px.center());
        p.rotate(e.box.rotationDeg);
        p.translate(-px.center());
    }
    const qreal radius = qMax(0.0, e.box.cornerRadiusPt * scale);
    if (e.box.backgroundEnabled) {
        p.setPen(Qt::NoPen);
        p.setBrush(e.box.backgroundColor);
        p.drawRoundedRect(px, radius, radius);
    }
    if (e.box.borderEnabled) {
        Qt::PenStyle style = Qt::SolidLine;
        if (e.box.borderStyle == TextBoxProperties::BorderStyle::Dashed) style = Qt::DashLine;
        if (e.box.borderStyle == TextBoxProperties::BorderStyle::Dotted) style = Qt::DotLine;
        QPen border(e.box.borderColor, qMax(0.5, e.box.borderWidthPt * scale), style);
        p.setPen(border); p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(px.adjusted(border.widthF()/2, border.widthF()/2,
                                     -border.widthF()/2, -border.widthF()/2), radius, radius);
    }

    const double sizePt = e.renderSizePt > 0.0 ? e.renderSizePt : e.fontSizePt;
    int pixelSize;
    if (sizePt > 0.0) {
        pixelSize = qMax(6, qRound(sizePt * scale));
    } else {
        const int lineCount = qMax(1, e.newText.count(u'\n') + 1);
        pixelSize = qMax(8, qRound(px.height() / lineCount / 0.72));
    }

    QFont f(e.fontFamily.isEmpty() ? QStringLiteral("Helvetica") : e.fontFamily);
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(pixelSize);
    f.setBold(e.bold);
    f.setItalic(e.italic);
    f.setUnderline(e.underline);
    if (!qFuzzyIsNull(e.box.characterSpacingPt))
        f.setLetterSpacing(QFont::AbsoluteSpacing, e.box.characterSpacingPt * scale);
    p.setFont(f);
    p.setPen(e.textColor.isValid() ? e.textColor : QColor(0x11, 0x11, 0x11));

    const QFontMetricsF fm(f);
    int kFlags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;
    if (e.box.horizontalAlign == TextBoxProperties::HorizontalAlign::Center)
        kFlags = Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap;
    else if (e.box.horizontalAlign == TextBoxProperties::HorizontalAlign::Right)
        kFlags = Qt::AlignRight | Qt::AlignTop | Qt::TextWordWrap;
    else if (e.box.horizontalAlign == TextBoxProperties::HorizontalAlign::Justify)
        kFlags = Qt::AlignJustify | Qt::AlignTop | Qt::TextWordWrap;
    const qreal pad = qMax(0.0, e.box.paddingPt * scale);
    QRectF target = px.adjusted(pad, pad, -pad, -pad);
    target.adjust(e.box.indentLevel * 18.0 * scale, 0, 0, 0);
    if (e.hasTextOrigin && qFuzzyIsNull(e.box.paddingPt)
            && e.box.verticalAlign == TextBoxProperties::VerticalAlign::Top) {
        const QPointF origin = (e.pdfBounds.topLeft() + e.textOriginOffset) * scale;
        target.translate(origin.x() - px.left(),
                         origin.y() - (px.top() + fm.ascent()));
    }

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
    if (!e.newText.contains(u'\n')) {
        QString line = e.newText;
        if (e.box.listStyle == TextBoxProperties::ListStyle::Bullets) line.prepend(QStringLiteral("• "));
        else if (e.box.listStyle == TextBoxProperties::ListStyle::Numbered) line.prepend(QStringLiteral("1. "));
        const qreal contentH = laidOutHeight(line);
        if (e.box.verticalAlign == TextBoxProperties::VerticalAlign::Center)
            target.moveTop(target.top() + qMax(0.0, (target.height() - contentH) / 2.0));
        else if (e.box.verticalAlign == TextBoxProperties::VerticalAlign::Bottom)
            target.moveTop(target.bottom() - contentH);
        drawBlock(line, target.top());
        p.restore();
        return;
    }

    const qreal step = stepPt > 0.0 ? stepPt * scale
                                    : fm.lineSpacing() * qMax(1.0, e.box.lineSpacingMultiplier);
    const qreal paragraph = qMax(0.0, e.box.paragraphSpacingPt * scale);
    const QStringList lines = e.newText.split(u'\n');
    qreal contentH = 0.0;
    for (const QString &line : lines)
        contentH += qMax(step, laidOutHeight(line)) + paragraph;
    if (!lines.isEmpty()) contentH -= paragraph;
    if (e.box.verticalAlign == TextBoxProperties::VerticalAlign::Center)
        target.moveTop(target.top() + qMax(0.0, (target.height() - contentH) / 2.0));
    else if (e.box.verticalAlign == TextBoxProperties::VerticalAlign::Bottom)
        target.moveTop(target.bottom() - contentH);
    qreal top = target.top();
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        if (e.box.listStyle == TextBoxProperties::ListStyle::Bullets) line.prepend(QStringLiteral("• "));
        else if (e.box.listStyle == TextBoxProperties::ListStyle::Numbered) line.prepend(QString::number(i + 1) + QStringLiteral(". "));
        drawBlock(line, top);
        top += qMax(step, laidOutHeight(line)) + paragraph;
    }
    p.restore();
}
