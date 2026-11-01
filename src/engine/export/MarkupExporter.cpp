#include "engine/export/MarkupExporter.hpp"

#include "engine/edit/ContentMap.hpp"
#include "engine/export/DocxXml.hpp"

#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace {

struct Inline {
    QString text;
    bool    bold   { false };
    bool    italic { false };
};

/// One piece of the flowing document: a paragraph, heading, list item, table or picture.
struct Piece {
    enum class Kind { Paragraph, Heading, Item, Table, Picture };

    Kind          kind { Kind::Paragraph };
    int           level { 0 };
    bool          ordered { false };
    int           number { 1 };
    QList<Inline> text;
    Qt::Alignment align { Qt::AlignLeft };
    DocxTable     table;
    DocxImage     picture;
    double        widthPt { 0.0 };
    QRectF        bounds;
    double        sizePt { 0.0 };
    int           page { 0 };
};

QString plain(const QList<Inline> &text)
{
    QString out;
    for (const Inline &part : text) out += part.text;
    return out;
}

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
        if (it.value() > most) { most = it.value(); best = it.key(); }
    return best / 2.0;
}

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
}

QList<Piece> piecesOf(const QList<DocxPage> &pages)
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
            pieces.append(scan);
            continue;
        }
        for (const DocxBlock &block : page.blocks) {
            if (isText(block)) {
                Piece piece = textPiece(block, body);
                piece.page = pg;
                if (plain(piece.text).trimmed().isEmpty()) continue;
                if (!pieces.isEmpty() && continues(pieces.last(), piece))
                    join(pieces.last(), piece);
                else
                    pieces.append(piece);
            } else if (block.kind == DocxBlock::Kind::Table && !block.table.cells.isEmpty()) {
                Piece piece;
                piece.kind  = Piece::Kind::Table;
                piece.table = block.table;
                pieces.append(piece);
            } else if (block.kind == DocxBlock::Kind::Picture && !block.picture.isNull()) {
                Piece piece;
                piece.kind    = Piece::Kind::Picture;
                piece.picture = block.picture;
                piece.widthPt = block.bounds.width();
                pieces.append(piece);
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

// ── Markdown ────────────────────────────────────────────────────────────────

QString mdEscape(const QString &text)
{
    static const QString kSpecial = QStringLiteral("\\`*_[]<>|");
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        if (kSpecial.contains(ch)) out += u'\\';
        out += ch;
    }
    return out;
}

QString mdInline(const QList<Inline> &text, bool styled)
{
    QString out;
    for (const Inline &part : text) {
        const QString mark = !styled ? QString{}
                           : part.bold && part.italic ? QStringLiteral("***")
                           : part.bold ? QStringLiteral("**")
                           : part.italic ? QStringLiteral("*") : QString{};
        const QString core = part.text.trimmed();
        if (mark.isEmpty() || core.isEmpty()) {
            out += mdEscape(part.text);
            continue;
        }
        // Emphasis must hug its text, so the spaces around it stay outside.
        const qsizetype lead = part.text.indexOf(core);
        out += part.text.left(lead) + mark + mdEscape(core) + mark
             + part.text.mid(lead + core.size());
    }
    out = out.trimmed();
    // A paragraph that starts like a heading or a list would be read as one.
    static const QRegularExpression mark(QStringLiteral("^(#|[-+=]\\s)"));
    static const QRegularExpression number(QStringLiteral("^\\d+(?=[.)]\\s)"));
    if (mark.match(out).hasMatch()) out.prepend(u'\\');
    else if (const auto m = number.match(out); m.hasMatch()) out.insert(m.capturedEnd(), u'\\');
    return out.replace(u'\n', QStringLiteral("  \n"));
}

QString mdTable(const DocxTable &table)
{
    const QList<QList<const DocxCell *>> rows = grid(table);
    QString out;
    for (int r = 0; r < rows.size(); ++r) {
        out += u'|';
        for (const DocxCell *cell : rows[r]) {
            const QString text = cell ? mdInline(cellText(*cell), true) : QString{};
            out += u' ' + QString(text).replace(QStringLiteral("  \n"), QStringLiteral("<br>"))
                 + QStringLiteral(" |");
        }
        out += u'\n';
        if (r == 0) {
            out += u'|';
            for (qsizetype c = 0; c < rows[r].size(); ++c) out += QStringLiteral(" --- |");
            out += u'\n';
        }
    }
    return out;
}

// ── HTML ────────────────────────────────────────────────────────────────────

QString htmlEscape(const QString &text)
{
    return text.toHtmlEscaped();
}

QString htmlInline(const QList<Inline> &text, bool styled)
{
    QString out;
    for (const Inline &part : text) {
        QString t = htmlEscape(part.text);
        if (styled && part.italic) t = QStringLiteral("<em>") + t + QStringLiteral("</em>");
        if (styled && part.bold)   t = QStringLiteral("<strong>") + t + QStringLiteral("</strong>");
        out += t;
    }
    return out.trimmed().replace(u'\n', QStringLiteral("<br>\n"));
}

QString htmlAlign(Qt::Alignment align)
{
    if (align & Qt::AlignHCenter) return QStringLiteral(" style=\"text-align:center\"");
    if (align & Qt::AlignRight)   return QStringLiteral(" style=\"text-align:right\"");
    return {};
}

QString htmlTable(const DocxTable &table)
{
    QString out = QStringLiteral("<table>\n");
    for (const QList<const DocxCell *> &row : grid(table)) {
        out += QStringLiteral("<tr>");
        for (qsizetype c = 0; c < row.size(); ++c) {
            const DocxCell *cell = row[c];
            if (!cell) {
                // Covered by a cell that spans it, or simply empty.
                bool covered = false;
                for (qsizetype k = 0; k < c; ++k)
                    if (row[k] && k + row[k]->colSpan > c) covered = true;
                if (!covered) out += QStringLiteral("<td></td>");
                continue;
            }
            out += QStringLiteral("<td");
            if (cell->colSpan > 1) out += QStringLiteral(" colspan=\"%1\"").arg(cell->colSpan);
            if (cell->align & Qt::AlignRight) out += QStringLiteral(" style=\"text-align:right\"");
            else if (cell->align & Qt::AlignHCenter)
                out += QStringLiteral(" style=\"text-align:center\"");
            out += u'>' + htmlInline(cellText(*cell), true) + QStringLiteral("</td>");
        }
        out += QStringLiteral("</tr>\n");
    }
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

}

bool MarkupExporter::exportToMarkdown(const QString &outputPath, const QList<DocxPage> &pages)
{
    const QList<Piece> pieces = piecesOf(pages);
    QString out;
    for (qsizetype i = 0; i < pieces.size(); ++i) {
        const Piece &piece = pieces[i];
        const bool listGoesOn = piece.kind == Piece::Kind::Item && i > 0
                             && pieces[i - 1].kind == Piece::Kind::Item;
        QString text;
        switch (piece.kind) {
        case Piece::Kind::Heading:
            text = QString(piece.level, u'#') + u' '
                 + mdInline(piece.text, false).replace(QStringLiteral("  \n"), QStringLiteral(" "));
            break;
        case Piece::Kind::Item:
            text = (piece.ordered ? QString::number(piece.number) + QStringLiteral(". ")
                                  : QStringLiteral("- "))
                 + mdInline(piece.text, true).replace(QStringLiteral("  \n"),
                                                      QStringLiteral("  \n   "));
            break;
        case Piece::Kind::Paragraph:
            text = mdInline(piece.text, true);
            break;
        case Piece::Kind::Table:
            text = mdTable(piece.table).trimmed();
            break;
        case Piece::Kind::Picture:
            continue;
        }
        if (!out.isEmpty()) out += listGoesOn ? QStringLiteral("\n") : QStringLiteral("\n\n");
        out += text;
    }
    return write(outputPath, out + u'\n');
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
        "td { border: 1px solid #d0d7de; padding: 0.3em 0.6em; vertical-align: top; }\n"
        "@media (prefers-color-scheme: dark) {"
        " body { color: #e6edf3; background: #0d1117; } td { border-color: #30363d; } }\n"
        "</style>\n</head>\n<body>\n");

    const QList<Piece> pieces = piecesOf(pages);
    QString openList;
    for (const Piece &piece : pieces) {
        const QString list = piece.kind != Piece::Kind::Item ? QString{}
                           : piece.ordered ? QStringLiteral("ol") : QStringLiteral("ul");
        if (openList != list && !openList.isEmpty()) {
            out += QStringLiteral("</") + openList + QStringLiteral(">\n");
            openList.clear();
        }
        switch (piece.kind) {
        case Piece::Kind::Heading:
            out += QStringLiteral("<h%1>").arg(piece.level) + htmlInline(piece.text, false)
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
            out += QStringLiteral("<li>") + htmlInline(piece.text, true) + QStringLiteral("</li>\n");
            break;
        case Piece::Kind::Paragraph:
            out += QStringLiteral("<p") + htmlAlign(piece.align) + u'>'
                 + htmlInline(piece.text, true) + QStringLiteral("</p>\n");
            break;
        case Piece::Kind::Table:
            out += htmlTable(piece.table);
            break;
        case Piece::Kind::Picture:
            out += htmlPicture(piece);
            break;
        }
    }
    if (!openList.isEmpty()) out += QStringLiteral("</") + openList + QStringLiteral(">\n");
    out += QStringLiteral("</body>\n</html>\n");
    return write(outputPath, out);
}
