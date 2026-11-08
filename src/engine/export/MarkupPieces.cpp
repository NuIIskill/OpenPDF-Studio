#include "engine/export/MarkupPieces.hpp"

#include "engine/edit/ContentMap.hpp"
#include "engine/export/DocxXml.hpp"

#include <QHash>
#include <QImage>
#include <QSet>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace MarkupPieces {

QString plain(const QList<Inline> &text)
{
    QString out;
    for (const Inline &part : text) out += part.text;
    return out;
}

namespace {

QList<Inline> inlinesOf(const DocxBlock &block)
{
    QList<Inline> out;
    for (const DocxXml::Span &span : DocxXml::paragraphSpans(block)) {
        QString text = span.text;
        text.replace(u'\t', u' ');
        if (!out.isEmpty() && out.last().bold == span.style.bold
                && out.last().italic == span.style.italic)
            out.last().text += text;
        else
            out.append({ text, span.style.bold, span.style.italic });
    }
    return out;
}

double sizeOf(const DocxBlock &block)
{
    double sum = 0.0;
    for (const ContentItem &line : block.lines) sum += DocxXml::fontSizeOf_(line);
    return block.lines.isEmpty() ? 0.0 : sum / block.lines.size();
}

bool isText(const DocxBlock &block)
{
    return (block.kind == DocxBlock::Kind::Paragraph || block.kind == DocxBlock::Kind::TextBox)
        && !block.lines.isEmpty();
}

}

// The size most of the running text is set in, which headings stand out from.
double bodySize(const QList<DocxPage> &pages)
{
    QHash<int, qsizetype> weight;
    for (const DocxPage &page : pages)
        for (const DocxBlock &block : page.blocks)
            if (isText(block))
                for (const ContentItem &line : block.lines)
                    weight[qRound(DocxXml::fontSizeOf_(line) * 2.0)] += line.text.size();
    int best = 20;
    qsizetype most = -1;
    for (auto it = weight.cbegin(); it != weight.cend(); ++it)
        if (it.value() > most || (it.value() == most && it.key() < best)) {
            most = it.value();
            best = it.key();
        }
    return best / 2.0;
}

namespace {

// Strips a leading list marker and tells what kind of list it opens.
bool takeMarker(QList<Inline> &text, bool *ordered, int *number)
{
    if (text.isEmpty()) return false;
    QString &first = text.first().text;
    static const QRegularExpression lead(QStringLiteral("^(\\S+)\\s+(?=\\S)"));
    const QRegularExpressionMatch match = lead.match(first);
    if (!match.hasMatch() || !isListMarker(match.captured(1))) return false;
    static const QRegularExpression numbered(QStringLiteral("^(\\d{1,3})[.)]$"));
    const QRegularExpressionMatch digits = numbered.match(match.captured(1));
    *ordered = digits.hasMatch();
    *number  = *ordered ? digits.captured(1).toInt() : 1;
    // A letter marker has no Markdown or HTML list of its own and stays in the text.
    if (!*ordered && match.captured(1).front().isLetter()) return true;
    first.remove(0, match.capturedLength());
    return true;
}

Piece textPiece(const DocxBlock &block, double body)
{
    Piece piece;
    piece.text   = inlinesOf(block);
    piece.align  = block.align;
    piece.bounds = block.bounds;
    piece.sizePt = sizeOf(block);
    piece.color  = block.lines.first().textColor;
    if (takeMarker(piece.text, &piece.ordered, &piece.number)) {
        piece.kind = Piece::Kind::Item;
        return piece;
    }

    const QString text = plain(piece.text).trimmed();
    if (text.size() > 200 || block.lines.size() > 3) return piece;
    const double ratio = sizeOf(block) / qMax(1.0, body);
    const bool allBold = std::all_of(piece.text.cbegin(), piece.text.cend(),
                                     [](const Inline &part) {
                                         return part.bold || part.text.trimmed().isEmpty();
                                     });
    static const QString kRunning = QStringLiteral(".,:;");
    piece.level = ratio >= 1.7 ? 1 : ratio >= 1.35 ? 2 : ratio >= 1.15 ? 3
                : allBold && ratio > 0.98 && block.lines.size() == 1 && text.size() <= 80
                      && !kRunning.contains(text.back()) ? 4 : 0;
    if (piece.level > 0) piece.kind = Piece::Kind::Heading;
    return piece;
}

// A paragraph cut where it runs past a picture goes on in the next block: no
// sentence ends before it, and it either starts in lower case or follows as
// the next line of the same column.
bool continues(const Piece &before, const Piece &next)
{
    if (before.kind != Piece::Kind::Paragraph || next.kind != Piece::Kind::Paragraph)
        return false;
    const QString a = plain(before.text).trimmed();
    const QString b = plain(next.text).trimmed();
    static const QString kEnds = QStringLiteral(".!?:;\u2026\u3002");
    if (a.isEmpty() || b.isEmpty() || kEnds.contains(a.back())
            || qAbs(before.sizePt - next.sizePt) > 0.5)
        return false;
    if (b.front().isLower()) return true;
    const double gap = next.bounds.top() - before.bounds.bottom();
    return before.page == next.page && gap > -2.0 && gap < before.sizePt * 0.8;
}

void join(Piece &before, const Piece &next)
{
    QString &end = before.text.last().text;
    while (!end.isEmpty() && end.back().isSpace()) end.chop(1);
    const bool hyphen = end.endsWith(u'-') && plain(next.text).trimmed().front().isLower();
    if (hyphen) end.chop(1);
    else before.text.append({ QStringLiteral(" "), false, false });
    before.text += next.text;
    before.bounds = before.bounds.united(next.bounds);
    before.lastPage = next.page;
    before.label    = before.label && next.label;
}

}

QList<Piece> piecesOf(const QList<DocxPage> &pages, QList<Piece> *boxes)
{
    const double body = bodySize(pages);
    QList<Piece> pieces;
    for (int pg = 0; pg < pages.size(); ++pg) {
        const DocxPage &page = pages[pg];
        if (page.blocks.isEmpty() && !page.background.isNull()) {
            Piece scan;
            scan.kind    = Piece::Kind::Picture;
            scan.picture = page.background;
            scan.widthPt = page.pageSizePt.width();
            scan.page    = scan.lastPage = pg;
            pieces.append(scan);
            continue;
        }
        for (const DocxBlock &block : page.blocks) {
            if (isText(block)) {
                Piece piece = textPiece(block, body);
                piece.page  = piece.lastPage = pg;
                piece.label = block.kind == DocxBlock::Kind::TextBox;
                if (plain(piece.text).trimmed().isEmpty()) continue;
                if (!pieces.isEmpty() && continues(pieces.last(), piece))
                    join(pieces.last(), piece);
                else
                    pieces.append(piece);
            } else if (block.kind == DocxBlock::Kind::Table && !block.table.cells.isEmpty()) {
                Piece piece;
                piece.kind   = Piece::Kind::Table;
                piece.table  = block.table;
                piece.bounds = block.bounds;
                piece.page   = piece.lastPage = pg;
                pieces.append(piece);
            } else if (block.kind == DocxBlock::Kind::Picture && !block.picture.isNull()) {
                Piece piece;
                piece.kind    = Piece::Kind::Picture;
                piece.picture = block.picture;
                piece.withLabels = block.withLabels;
                piece.widthPt = block.bounds.width();
                piece.bounds  = block.bounds;
                piece.page    = piece.lastPage = pg;
                pieces.append(piece);
            } else if (block.kind == DocxBlock::Kind::Shape && boxes && block.fillColor.isValid()) {
                Piece box;
                box.kind   = Piece::Kind::Box;
                box.bounds = block.bounds;
                box.color  = block.fillColor;
                box.page   = box.lastPage = pg;
                boxes->append(box);
            }
        }
    }
    return pieces;
}

QList<Inline> cellText(const DocxCell &cell)
{
    DocxBlock block;
    block.lines = { cell.item };
    return inlinesOf(block);
}

// Rows of cells, each cell at its column; spanned columns stay empty.
QList<QList<const DocxCell *>> grid(const DocxTable &table)
{
    int cols = int(table.colWidthsPt.size());
    for (const DocxCell &cell : table.cells) cols = qMax(cols, cell.col + cell.colSpan);
    QList<QList<const DocxCell *>> rows(qMax(1, table.rowCount),
                                        QList<const DocxCell *>(qMax(1, cols), nullptr));
    for (const DocxCell &cell : table.cells)
        if (cell.row >= 0 && cell.row < rows.size() && cell.col >= 0 && cell.col < cols)
            rows[cell.row][cell.col] = &cell;
    return rows;
}

bool write(const QString &path, const QString &text)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(text.toUtf8());
    return file.commit();
}

namespace {

// Nothing but one colour inside its edge: a frame or a plain area.
bool emptyInside(const QImage &image)
{
    const int inset = qMax(4, qMin(image.width(), image.height()) / 25);
    const QRect inner = image.rect().adjusted(inset, inset, -inset, -inset);
    if (inner.width() < 8 || inner.height() < 8) return false;
    const int step = qMax(1, int(std::sqrt(double(inner.width()) * inner.height() / 250000.0)));
    QHash<QRgb, qsizetype> counts;
    qsizetype total = 0;
    for (int y = inner.top(); y <= inner.bottom(); y += step)
        for (int x = inner.left(); x <= inner.right(); x += step, ++total)
            ++counts[image.pixel(x, y)];
    QRgb flat = 0;
    qsizetype most = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() > most || (it.value() == most && it.key() < flat)) {
            most = it.value();
            flat = it.key();
        }
    qsizetype ink = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        const QRgb c = it.key();
        if (qMax(qMax(qAbs(qRed(c) - qRed(flat)), qAbs(qGreen(c) - qGreen(flat))),
                 qMax(qAbs(qBlue(c) - qBlue(flat)), qAbs(qAlpha(c) - qAlpha(flat)))) > 40)
            ink += it.value();
    }
    return ink * 2000 <= total;
}

// Only addresses a browser opens as a page or a mail; a script link in the
// PDF would otherwise run in the exported page.
bool safeUrl(const QString &url)
{
    static const QRegularExpression scheme(QStringLiteral("^(https?|ftp|mailto):"),
                                           QRegularExpression::CaseInsensitiveOption);
    return scheme.match(url).hasMatch();
}

bool overlaps(const QList<Anchor> &anchors, qsizetype start, qsizetype end)
{
    return std::any_of(anchors.cbegin(), anchors.cend(), [&](const Anchor &a) {
        return start < a.end && a.start < end;
    });
}

}

// The link annotations of the text's pages that lie on words of it, each used
// once, and the web and mail addresses written out in it.
QList<Anchor> anchorsIn(const QList<Inline> &text, QList<QList<DocxLink>> &links,
                        int firstPage, int lastPage)
{
    QList<Anchor> anchors;
    const QString flat = plain(text);
    for (int pg = qMax(0, firstPage); pg <= lastPage && pg < links.size(); ++pg) {
        QList<DocxLink> &open = links[pg];
        for (qsizetype i = 0; i < open.size(); ++i) {
            const QStringList words = open[i].text.split(u' ', Qt::SkipEmptyParts);
            if (words.isEmpty() || !safeUrl(open[i].url)) continue;
            QStringList escaped;
            for (const QString &word : words) escaped << QRegularExpression::escape(word);
            const QRegularExpressionMatch match =
                QRegularExpression(escaped.join(QStringLiteral("\\s+"))).match(flat);
            if (!match.hasMatch() || overlaps(anchors, match.capturedStart(), match.capturedEnd()))
                continue;
            anchors.append({ match.capturedStart(), match.capturedEnd(), open[i].url });
            open.removeAt(i--);
        }
    }
    static const QRegularExpression written(QStringLiteral(
        "\\b(?:https?://|www\\.)[^\\s<>\"]+|\\b[\\w.+-]+@[\\w-]+(?:\\.[\\w-]+)+"));
    static const QRegularExpression trailing(QStringLiteral("[.,;:!?)\\]}'\"]+$"));
    for (auto it = written.globalMatch(flat); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        QString address = match.captured();
        address.remove(trailing);
        const qsizetype end = match.capturedStart() + address.size();
        if (overlaps(anchors, match.capturedStart(), end)) continue;
        const QString url = address.contains(u'@') && !address.contains(u'/')
                                ? QStringLiteral("mailto:") + address
                          : address.startsWith(QLatin1String("www."), Qt::CaseInsensitive)
                                ? QStringLiteral("https://") + address
                                : address;
        anchors.append({ match.capturedStart(), end, url });
    }
    return anchors;
}

QList<Run> runsOf(const QList<Inline> &text, const QList<Anchor> &anchors)
{
    QList<Run> runs;
    qsizetype at = 0;
    for (const Inline &part : text) {
        qsizetype from = 0;
        while (from < part.text.size()) {
            const qsizetype pos = at + from;
            qsizetype next = at + part.text.size();
            int inside = -1;
            for (int a = 0; a < anchors.size(); ++a) {
                if (pos >= anchors[a].start && pos < anchors[a].end) {
                    inside = a;
                    next = qMin(next, anchors[a].end);
                } else if (anchors[a].start > pos) {
                    next = qMin(next, anchors[a].start);
                }
            }
            if (runs.isEmpty() || runs.last().anchor != inside) runs.append({ {}, inside });
            runs.last().text.append({ part.text.mid(from, next - pos), part.bold, part.italic });
            from = next - at;
        }
        at += part.text.size();
    }
    return runs;
}

bool boldRow(const QList<const DocxCell *> &row)
{
    bool any = false;
    for (const DocxCell *cell : row) {
        if (!cell) continue;
        for (const Inline &part : cellText(*cell)) {
            if (part.text.trimmed().isEmpty()) continue;
            if (!part.bold) return false;
            any = true;
        }
    }
    return any;
}

// Running heads and feet, the same on most pages but for their numbers, and
// the rules beside them have no place in a page that does not break.
void dropRunning(QList<Piece> &pieces, const QList<DocxPage> &pages)
{
    if (pages.size() < 2) return;
    const double body = bodySize(pages);
    static const QRegularExpression digits(QStringLiteral("\\d+"));
    const auto keyOf = [&](const Piece &piece) -> QString {
        if (piece.page != piece.lastPage || piece.page >= pages.size()) return {};
        const double height = pages[piece.page].pageSizePt.height();
        const bool head = piece.bounds.bottom() < height * 0.1;
        const bool foot = piece.bounds.top() > height * 0.9;
        if (!head && !foot) return {};
        const QString edge = head ? QStringLiteral("head:") : QStringLiteral("foot:");
        if (piece.kind == Piece::Kind::Picture)
            return piece.bounds.height() > 4.0 ? QString{}
                 : edge + QStringLiteral("rule:") + QString::number(qRound(piece.bounds.top() / 3.0));
        if ((piece.kind != Piece::Kind::Paragraph && piece.kind != Piece::Kind::Heading)
                || piece.sizePt > body * 1.1)
            return {};
        return edge + plain(piece.text).simplified().replace(digits, QStringLiteral("#"));
    };
    QList<QString> keys;
    QHash<QString, QSet<int>> onPages;
    for (const Piece &piece : std::as_const(pieces)) {
        keys.append(keyOf(piece));
        if (!keys.last().isEmpty()) onPages[keys.last()].insert(piece.page);
    }
    const int needed = qMax(2, int(pages.size() + 1) / 2);
    for (qsizetype i = pieces.size() - 1; i >= 0; --i)
        if (!keys[i].isEmpty() && onPages.value(keys[i]).size() >= needed) pieces.removeAt(i);
}

// A grey line under the title, smaller than it, is its subtitle.
void markSubtitles(QList<Piece> &pieces)
{
    int top = 0;
    for (const Piece &piece : std::as_const(pieces))
        if (piece.kind == Piece::Kind::Heading && (top == 0 || piece.level < top)) top = piece.level;
    for (qsizetype i = 1; i < pieces.size(); ++i) {
        Piece &piece = pieces[i];
        const Piece &title = pieces[i - 1];
        if (title.kind != Piece::Kind::Heading || title.level != top || piece.page != title.page
                || (piece.kind != Piece::Kind::Heading && piece.kind != Piece::Kind::Paragraph)
                || piece.sizePt >= title.sizePt || !piece.color.isValid()
                || piece.color.hslSaturation() > 40 || piece.color.lightness() < 70
                || piece.color.lightness() > 200)
            continue;
        piece.kind     = Piece::Kind::Paragraph;
        piece.subtitle = true;
    }
}

// The heading levels the document uses, closed up to start at 1 without gaps.
void closeHeadingLevels(QList<Piece> &pieces)
{
    QList<int> used;
    for (const Piece &piece : std::as_const(pieces))
        if (piece.kind == Piece::Kind::Heading && !used.contains(piece.level))
            used.append(piece.level);
    std::sort(used.begin(), used.end());
    for (Piece &piece : pieces)
        if (piece.kind == Piece::Kind::Heading) piece.level = int(used.indexOf(piece.level)) + 1;
}

// The box each piece stands in, or -1: a coloured area holding text of its page
// is kept as a box around that text.
QList<int> boxesOf(const QList<Piece> &pieces, const QList<Piece> &boxes,
                   const QList<DocxPage> &pages)
{
    QList<int> in(pieces.size(), -1);
    for (int b = 0; b < boxes.size(); ++b) {
        const QRectF &bounds = boxes[b].bounds;
        const QSizeF page = pages.value(boxes[b].page).pageSizePt;
        if (bounds.height() < 8.0
                || bounds.width() * bounds.height() > page.width() * page.height() * 0.5)
            continue;
        const QRectF area = bounds.adjusted(-1.0, -1.0, 1.0, 1.0);
        for (qsizetype i = 0; i < pieces.size(); ++i)
            if (in[i] < 0 && pieces[i].page == boxes[b].page && area.contains(pieces[i].bounds))
                in[i] = b;
    }
    return in;
}

QImage decoded(const DocxImage &picture)
{
    return QImage::fromData(picture.data).convertToFormat(QImage::Format_ARGB32);
}

bool positioned(const Piece &piece)
{
    return piece.kind == Piece::Kind::Picture && !piece.bounds.isEmpty();
}

// A frame or a plain area drawn behind text has no place in a page that flows,
// so it is left out where it holds text of its page and nothing else is drawn
// inside it.
void dropBackdrops(QList<Piece> &pieces)
{
    for (qsizetype i = pieces.size() - 1; i >= 0; --i) {
        const Piece &picture = pieces[i];
        if (!positioned(picture)) continue;
        bool text = false;
        bool drawing = false;
        for (qsizetype k = 0; k < pieces.size(); ++k) {
            const Piece &other = pieces[k];
            if (k == i || other.page != picture.page || !picture.bounds.contains(other.bounds))
                continue;
            if (other.kind == Piece::Kind::Picture) drawing = true;
            else text = true;
        }
        if (text && !drawing && emptyInside(decoded(picture.picture))) pieces.removeAt(i);
    }
}

}
