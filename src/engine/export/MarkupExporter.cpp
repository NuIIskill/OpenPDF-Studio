#include "engine/export/MarkupExporter.hpp"

#include "engine/export/MarkupPieces.hpp"

#include <QRegularExpression>

#include <algorithm>

using namespace MarkupPieces;

namespace {

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

QString mdUrl(QString url)
{
    return url.replace(u' ', QStringLiteral("%20")).replace(u'(', QStringLiteral("%28"))
              .replace(u')', QStringLiteral("%29")).replace(u'<', QStringLiteral("%3C"))
              .replace(u'>', QStringLiteral("%3E")).replace(u'|', QStringLiteral("%7C"));
}

QString mdRun(const QList<Inline> &text, bool styled)
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
    return out;
}

QString mdInline(const QList<Inline> &text, bool styled, const QList<Anchor> &anchors = {})
{
    QString out;
    for (const Run &run : runsOf(text, anchors))
        out += run.anchor < 0 ? mdRun(run.text, styled)
             : u'[' + mdRun(run.text, styled) + QStringLiteral("](")
                   + mdUrl(anchors[run.anchor].url) + u')';
    out = out.trimmed();
    // A paragraph that starts like a heading or a list would be read as one.
    static const QRegularExpression mark(QStringLiteral("^(#|[-+=]\\s)"));
    static const QRegularExpression number(QStringLiteral("^\\d+(?=[.)]\\s)"));
    if (mark.match(out).hasMatch()) out.prepend(u'\\');
    else if (const auto m = number.match(out); m.hasMatch()) out.insert(m.capturedEnd(), u'\\');
    return out.replace(u'\n', QStringLiteral("  \n"));
}

QString mdTable(const Piece &piece, QList<QList<DocxLink>> &links)
{
    const QList<QList<const DocxCell *>> rows = grid(piece.table);
    // The first row is the header in Markdown; set in bold it needs no marks of its own.
    const bool boldHeader = boldRow(rows.first());
    QString out;
    for (int r = 0; r < rows.size(); ++r) {
        out += u'|';
        for (const DocxCell *cell : rows[r]) {
            QString text;
            if (cell) {
                const QList<Inline> inlines = cellText(*cell);
                text = mdInline(inlines, r > 0 || !boldHeader,
                                anchorsIn(inlines, links, piece.page, piece.lastPage));
            }
            out += u' ' + text.replace(QStringLiteral("  \n"), QStringLiteral("<br>"))
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

// The labels of a chart, which Markdown shows without its picture, are put
// together line by line as they stand side by side in it.
void groupLabels(QList<Piece> &pieces)
{
    QList<bool> gone(pieces.size(), false);
    for (const Piece &picture : std::as_const(pieces)) {
        if (!positioned(picture) || picture.withLabels.isNull()) continue;
        QList<qsizetype> labels;
        for (qsizetype k = 0; k < pieces.size(); ++k)
            if (!gone[k] && pieces[k].label && pieces[k].kind == Piece::Kind::Paragraph
                    && pieces[k].page == picture.page
                    && picture.bounds.contains(pieces[k].bounds.center()))
                labels.append(k);
        if (labels.size() < 2) continue;

        QList<qsizetype> order = labels;
        std::sort(order.begin(), order.end(), [&](qsizetype a, qsizetype b) {
            return pieces[a].bounds.center().y() < pieces[b].bounds.center().y();
        });
        QList<QList<qsizetype>> rows;
        for (qsizetype k : std::as_const(order)) {
            const QRectF &box = pieces[k].bounds;
            if (!rows.isEmpty()) {
                const QRectF &first = pieces[rows.last().first()].bounds;
                if (qAbs(box.center().y() - first.center().y())
                        < qMin(box.height(), first.height()) * 0.6) {
                    rows.last().append(k);
                    continue;
                }
            }
            rows.append(QList<qsizetype>{ k });
        }

        Piece &kept = pieces[labels.first()];
        QList<Inline> text;
        for (QList<qsizetype> &row : rows) {
            std::sort(row.begin(), row.end(), [&](qsizetype a, qsizetype b) {
                return pieces[a].bounds.left() < pieces[b].bounds.left();
            });
            if (!text.isEmpty()) text.append({ QStringLiteral("\n"), false, false });
            for (qsizetype n = 0; n < row.size(); ++n) {
                if (n > 0) text.append({ QStringLiteral(" "), false, false });
                text += pieces[row[n]].text;
            }
        }
        kept.text  = text;
        kept.align = Qt::AlignLeft;
        for (qsizetype k : std::as_const(labels)) gone[k] = k != labels.first();
    }
    for (qsizetype k = pieces.size() - 1; k >= 0; --k)
        if (gone[k]) pieces.removeAt(k);
}

QString quoted(const QString &text)
{
    QStringList lines = text.split(u'\n');
    for (QString &line : lines) line = line.isEmpty() ? QStringLiteral(">") : QStringLiteral("> ") + line;
    return lines.join(u'\n');
}

}

bool MarkupExporter::exportToMarkdown(const QString &outputPath, const QList<DocxPage> &pages)
{
    QList<Piece> boxes;
    QList<Piece> pieces = piecesOf(pages, &boxes);
    dropRunning(pieces, pages);
    dropBackdrops(pieces);
    groupLabels(pieces);
    markSubtitles(pieces);
    closeHeadingLevels(pieces);
    const QList<int> inBox = boxesOf(pieces, boxes, pages);
    QList<QList<DocxLink>> links;
    for (const DocxPage &page : pages) links.append(page.links);

    QString out;
    int previous = -1;
    for (qsizetype i = 0; i < pieces.size(); ++i) {
        const Piece &piece = pieces[i];
        const QList<Anchor> anchors = anchorsIn(piece.text, links, piece.page, piece.lastPage);
        QString text;
        switch (piece.kind) {
        case Piece::Kind::Heading:
            text = QString(piece.level, u'#') + u' '
                 + mdInline(piece.text, false, anchors).replace(QStringLiteral("  \n"),
                                                                QStringLiteral(" "));
            break;
        case Piece::Kind::Item:
            text = (piece.ordered ? QString::number(piece.number) + QStringLiteral(". ")
                                  : QStringLiteral("- "))
                 + mdInline(piece.text, true, anchors).replace(QStringLiteral("  \n"),
                                                               QStringLiteral("  \n   "));
            break;
        case Piece::Kind::Paragraph:
            text = mdInline(piece.text, true, anchors);
            break;
        case Piece::Kind::Table:
            text = mdTable(piece, links).trimmed();
            break;
        case Piece::Kind::Picture:
            // A thin wide line is a rule between parts of the text; pictures are left out.
            if (piece.bounds.isEmpty() || piece.bounds.height() > 4.0 || piece.bounds.width() < 100.0)
                continue;
            text = QStringLiteral("---");
            break;
        case Piece::Kind::Box:
            continue;
        }
        const bool listGoesOn = piece.kind == Piece::Kind::Item && previous >= 0
                             && pieces[previous].kind == Piece::Kind::Item;
        const bool sameBox = previous >= 0 && inBox[i] == inBox[previous];
        if (inBox[i] >= 0) text = quoted(text);
        if (!out.isEmpty())
            out += listGoesOn && sameBox ? QStringLiteral("\n")
                 : sameBox && inBox[i] >= 0 ? QStringLiteral("\n>\n") : QStringLiteral("\n\n");
        out += text;
        previous = int(i);
    }
    return write(outputPath, out + u'\n');
}
