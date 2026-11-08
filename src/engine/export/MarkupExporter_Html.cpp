#include "engine/export/MarkupExporter.hpp"

#include "engine/export/DocxXml.hpp"
#include "engine/export/MarkupPieces.hpp"

#include <QHash>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>

#include <algorithm>

using namespace MarkupPieces;

namespace {

// ── HTML ────────────────────────────────────────────────────────────────────

QString htmlEscape(const QString &text)
{
    return text.toHtmlEscaped();
}

QString htmlInline(const QList<Inline> &text, bool styled, const QList<Anchor> &anchors = {})
{
    QString out;
    for (const Run &run : runsOf(text, anchors)) {
        if (run.anchor >= 0)
            out += QStringLiteral("<a href=\"") + htmlEscape(anchors[run.anchor].url)
                 + QStringLiteral("\">");
        for (const Inline &part : run.text) {
            QString t = htmlEscape(part.text);
            if (styled && part.italic) t = QStringLiteral("<em>") + t + QStringLiteral("</em>");
            if (styled && part.bold)   t = QStringLiteral("<strong>") + t + QStringLiteral("</strong>");
            out += t;
        }
        if (run.anchor >= 0) out += QStringLiteral("</a>");
    }
    return out.trimmed().replace(u'\n', QStringLiteral("<br>\n"));
}

QString htmlAlign(Qt::Alignment align)
{
    if (align & Qt::AlignHCenter) return QStringLiteral(" style=\"text-align:center\"");
    if (align & Qt::AlignRight)   return QStringLiteral(" style=\"text-align:right\"");
    return {};
}

QString cellStyle(const DocxCell &cell)
{
    QStringList style;
    if (cell.align & Qt::AlignRight)        style << QStringLiteral("text-align:right");
    else if (cell.align & Qt::AlignHCenter) style << QStringLiteral("text-align:center");
    // The text keeps the colour it was legible in on the shading, also in dark mode.
    if (cell.shading.isValid() && cell.shading.alpha() > 0 && cell.shading != QColor(Qt::white))
        style << QStringLiteral("background:%1").arg(cell.shading.name())
              << QStringLiteral("color:%1").arg(cell.shading.lightness() > 128
                                                    ? QStringLiteral("#1f2328")
                                                    : QStringLiteral("#ffffff"));
    return style.isEmpty() ? QString{}
                           : QStringLiteral(" style=\"") + style.join(u';') + u'"';
}

QString htmlTable(const Piece &piece, QList<QList<DocxLink>> &links)
{
    const QList<QList<const DocxCell *>> rows = grid(piece.table);
    // A first row set in bold over a body that is not is the table's header.
    const bool header = rows.size() > 1 && boldRow(rows.first()) && !boldRow(rows[1]);
    QString out = piece.table.hasBorders ? QStringLiteral("<table>\n")
                                         : QStringLiteral("<table class=\"plain\">\n");
    for (qsizetype r = 0; r < rows.size(); ++r) {
        const QList<const DocxCell *> &row = rows[r];
        const bool head = header && r == 0;
        const QString tag = head ? QStringLiteral("th") : QStringLiteral("td");
        if (head) out += QStringLiteral("<thead>\n");
        if (header && r == 1) out += QStringLiteral("<tbody>\n");
        out += QStringLiteral("<tr>");
        for (qsizetype c = 0; c < row.size(); ++c) {
            const DocxCell *cell = row[c];
            if (!cell) {
                // Covered by a cell that spans it, or simply empty.
                bool covered = false;
                for (qsizetype k = 0; k < c; ++k)
                    if (row[k] && k + row[k]->colSpan > c) covered = true;
                if (!covered) out += u'<' + tag + QStringLiteral("></") + tag + u'>';
                continue;
            }
            out += u'<' + tag;
            if (cell->colSpan > 1) out += QStringLiteral(" colspan=\"%1\"").arg(cell->colSpan);
            const QList<Inline> text = cellText(*cell);
            out += cellStyle(*cell) + u'>'
                 + htmlInline(text, !head, anchorsIn(text, links, piece.page, piece.lastPage))
                 + QStringLiteral("</") + tag + u'>';
        }
        out += QStringLiteral("</tr>\n");
        if (head) out += QStringLiteral("</thead>\n");
    }
    if (header) out += QStringLiteral("</tbody>\n");
    return out + QStringLiteral("</table>\n");
}

QString htmlPicture(const Piece &piece)
{
    const QString mime = piece.picture.ext == QLatin1String("png") ? QStringLiteral("image/png")
                                                                   : QStringLiteral("image/jpeg");
    return QStringLiteral("<p><img alt=\"\" width=\"%1\" src=\"data:%2;base64,")
               .arg(qRound(piece.widthPt * 96.0 / 72.0)).arg(mime)
         + QString::fromLatin1(piece.picture.data.toBase64()) + QStringLiteral("\"></p>\n");
}

// Text in the space between two pictures that neither of them holds.
bool textBetween(const QList<Piece> &pieces, const Piece &a, const Piece &b)
{
    const QRectF span = a.bounds.united(b.bounds);
    return std::any_of(pieces.cbegin(), pieces.cend(), [&](const Piece &text) {
        return text.kind != Piece::Kind::Picture && text.page == a.page
            && span.intersects(text.bounds) && !a.bounds.contains(text.bounds)
            && !b.bounds.contains(text.bounds);
    });
}

// A drawing reaches the export as one picture for each of its parts, each cut
// to its own box. Stacked one under the other they make no sense, so parts
// that overlap or lie close with no text between them are laid back together
// into one picture.
void joinDrawings(QList<Piece> &pieces)
{
    constexpr double kNearPt = 18.0;
    QList<qsizetype> group(pieces.size());
    for (qsizetype i = 0; i < pieces.size(); ++i) group[i] = i;
    const auto root = [&group](qsizetype i) {
        while (group[i] != i) i = group[i] = group[group[i]];
        return i;
    };
    for (qsizetype i = 0; i < pieces.size(); ++i) {
        if (!positioned(pieces[i])) continue;
        const QRectF near = pieces[i].bounds.adjusted(-kNearPt, -kNearPt, kNearPt, kNearPt);
        for (qsizetype k = i + 1; k < pieces.size(); ++k)
            if (positioned(pieces[k]) && pieces[k].page == pieces[i].page
                    && near.intersects(pieces[k].bounds)
                    && (pieces[i].bounds.intersects(pieces[k].bounds)
                        || !textBetween(pieces, pieces[i], pieces[k])))
                group[root(k)] = root(i);
    }

    QList<qsizetype> gone;
    for (qsizetype first = 0; first < pieces.size(); ++first) {
        if (!positioned(pieces[first]) || root(first) != first) continue;
        QList<qsizetype> members;
        for (qsizetype k = first; k < pieces.size(); ++k)
            if (positioned(pieces[k]) && root(k) == first) members.append(k);
        if (members.size() < 2) continue;

        QRectF united;
        QList<QImage> images;
        double pxPerPt = 0.0;
        for (qsizetype k : std::as_const(members)) {
            united = united.united(pieces[k].bounds);
            images.append(decoded(pieces[k].picture));
            pxPerPt = qMax(pxPerPt, images.last().width() / qMax(1.0, pieces[k].bounds.width()));
        }
        pxPerPt = qBound(0.5, pxPerPt, 3000.0 / qMax(1.0, united.width()));

        // The paper the parts were cut from fills what lies between them; the
        // corners of the larger parts count for more.
        QHash<QRgb, double> corners;
        for (qsizetype n = 0; n < images.size(); ++n) {
            const QImage &image = images[n];
            if (image.isNull()) continue;
            const QRectF &b = pieces[members[n]].bounds;
            for (const QPoint &p : { QPoint(0, 0), QPoint(image.width() - 1, 0),
                                     QPoint(0, image.height() - 1),
                                     QPoint(image.width() - 1, image.height() - 1) })
                corners[image.pixel(p)] += b.width() * b.height();
        }
        QRgb paper = qRgba(0, 0, 0, 0);
        double most = 0.0;
        for (auto it = corners.cbegin(); it != corners.cend(); ++it)
            if (it.value() > most || (it.value() == most && it.key() < paper)) {
                most  = it.value();
                paper = it.key();
            }

        QImage canvas(qMax(1, qRound(united.width() * pxPerPt)),
                      qMax(1, qRound(united.height() * pxPerPt)), QImage::Format_ARGB32);
        canvas.fill(QColor::fromRgba(paper));
        QList<qsizetype> order(members.size());
        for (qsizetype n = 0; n < order.size(); ++n) order[n] = n;
        // Larger parts first, so an image placed over a drawn area stays on top.
        std::sort(order.begin(), order.end(), [&](qsizetype a, qsizetype b) {
            const QRectF &ra = pieces[members[a]].bounds;
            const QRectF &rb = pieces[members[b]].bounds;
            return ra.width() * ra.height() > rb.width() * rb.height();
        });
        {
            QPainter painter(&canvas);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            for (qsizetype n : std::as_const(order)) {
                const QRectF &b = pieces[members[n]].bounds;
                painter.drawImage(QRectF((b.topLeft() - united.topLeft()) * pxPerPt,
                                         b.size() * pxPerPt), images[n]);
            }
        }
        QByteArray png;
        if (!DocxXml::encodePng(canvas, &png)) continue;
        Piece &kept = pieces[first];
        kept.picture = { png, QStringLiteral("png") };
        kept.bounds  = united;
        kept.widthPt = united.width();
        gone += members.mid(1);
    }
    std::sort(gone.begin(), gone.end(), std::greater<>());
    for (qsizetype k : std::as_const(gone)) pieces.removeAt(k);
}

// A chart keeps its labels in its picture: the picture as the page shows it
// takes the place of the cut out one, and the labels go.
void bakeLabels(QList<Piece> &pieces)
{
    QList<bool> baked(pieces.size(), false);
    for (Piece &picture : pieces) {
        if (!positioned(picture) || picture.withLabels.isNull()) continue;
        picture.picture = picture.withLabels;
        for (qsizetype k = 0; k < pieces.size(); ++k)
            if (pieces[k].label && pieces[k].page == picture.page
                    && picture.bounds.contains(pieces[k].bounds.center()))
                baked[k] = true;
    }
    for (qsizetype k = pieces.size() - 1; k >= 0; --k)
        if (baked[k]) pieces.removeAt(k);
}

QString boxStyle(const QColor &fill)
{
    return QStringLiteral(" style=\"background:%1;border-color:%2;color:%3\"")
        .arg(fill.name(), fill.darker(125).name(),
             fill.lightness() > 128 ? QStringLiteral("#1f2328") : QStringLiteral("#ffffff"));
}

}

bool MarkupExporter::exportToHtml(const QString &outputPath, const QList<DocxPage> &pages,
                                  const QString &title)
{
    QString out = QStringLiteral(
        "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>")
        + htmlEscape(title) + QStringLiteral(
        "</title>\n<style>\n"
        "body { max-width: 48em; margin: 2em auto; padding: 0 1em; line-height: 1.5;"
        " font-family: system-ui, sans-serif; color: #1f2328; background: #fff; }\n"
        "img { max-width: 100%; height: auto; }\n"
        "table { border-collapse: collapse; margin: 1em 0; }\n"
        "td, th { border: 1px solid #d0d7de; padding: 0.3em 0.6em; vertical-align: top; }\n"
        "th { text-align: left; }\n"
        ".plain td, .plain th { border: none; }\n"
        ".box { border: 1px solid; border-radius: 4px; padding: 0 1em; margin: 1em 0; }\n"
        ".subtitle { font-size: 1.15em; opacity: 0.75; }\n"
        "hr { border: none; border-top: 1px solid #d0d7de; }\n"
        "a { color: #0969da; }\n"
        "@media (prefers-color-scheme: dark) {"
        " body { color: #e6edf3; background: #0d1117; } td, th { border-color: #30363d; }"
        " hr { border-top-color: #30363d; } a { color: #4493f8; } .box a { color: #0969da; } }\n"
        "</style>\n</head>\n<body>\n");

    QList<Piece> boxes;
    QList<Piece> pieces = piecesOf(pages, &boxes);
    dropRunning(pieces, pages);
    dropBackdrops(pieces);
    bakeLabels(pieces);
    joinDrawings(pieces);
    markSubtitles(pieces);
    closeHeadingLevels(pieces);
    const QList<int> inBox = boxesOf(pieces, boxes, pages);
    QList<QList<DocxLink>> links;
    for (const DocxPage &page : pages) links.append(page.links);

    QString openList;
    int openBox = -1;
    for (qsizetype i = 0; i < pieces.size(); ++i) {
        const Piece &piece = pieces[i];
        const QString list = piece.kind != Piece::Kind::Item ? QString{}
                           : piece.ordered ? QStringLiteral("ol") : QStringLiteral("ul");
        if (!openList.isEmpty() && (openList != list || inBox[i] != openBox)) {
            out += QStringLiteral("</") + openList + QStringLiteral(">\n");
            openList.clear();
        }
        if (inBox[i] != openBox) {
            if (openBox >= 0) out += QStringLiteral("</div>\n");
            openBox = inBox[i];
            if (openBox >= 0)
                out += QStringLiteral("<div class=\"box\"") + boxStyle(boxes[openBox].color)
                     + QStringLiteral(">\n");
        }
        const QList<Anchor> anchors = anchorsIn(piece.text, links, piece.page, piece.lastPage);
        switch (piece.kind) {
        case Piece::Kind::Heading:
            out += QStringLiteral("<h%1").arg(piece.level) + htmlAlign(piece.align) + u'>'
                 + htmlInline(piece.text, false, anchors)
                 + QStringLiteral("</h%1>\n").arg(piece.level);
            break;
        case Piece::Kind::Item:
            if (openList.isEmpty()) {
                openList = list;
                out += u'<' + list;
                if (piece.ordered && piece.number != 1)
                    out += QStringLiteral(" start=\"%1\"").arg(piece.number);
                out += QStringLiteral(">\n");
            }
            out += QStringLiteral("<li>") + htmlInline(piece.text, true, anchors)
                 + QStringLiteral("</li>\n");
            break;
        case Piece::Kind::Paragraph:
            out += QStringLiteral("<p")
                 + (piece.subtitle ? QStringLiteral(" class=\"subtitle\"") : QString{})
                 + htmlAlign(piece.align) + u'>' + htmlInline(piece.text, true, anchors)
                 + QStringLiteral("</p>\n");
            break;
        case Piece::Kind::Table:
            out += htmlTable(piece, links);
            break;
        case Piece::Kind::Picture:
            // A thin wide line is a rule between parts of the text.
            if (!piece.bounds.isEmpty() && piece.bounds.height() <= 4.0
                    && piece.bounds.width() >= 100.0)
                out += QStringLiteral("<hr>\n");
            else
                out += htmlPicture(piece);
            break;
        case Piece::Kind::Box:
            break;
        }
    }
    if (!openList.isEmpty()) out += QStringLiteral("</") + openList + QStringLiteral(">\n");
    if (openBox >= 0) out += QStringLiteral("</div>\n");
    out += QStringLiteral("</body>\n</html>\n");
    return write(outputPath, out);
}
