#include "engine/import/TextDocumentPdf.hpp"

#include "engine/import/DocumentBuilder.hpp"

#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QFileInfo>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QHash>
#include <QList>
#include <QFont>
#include <QPen>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

namespace {

constexpr int MaxPages = 5000;

QString tr(const char *text) { return QCoreApplication::translate("DocumentImport", text); }

}

// QTextDocument::print is deliberately not used: it lays out on a clone, and
// whether the images registered as resources survive that clone depends on the
// Qt version. The loop below is what print does anyway, minus the guessing.
bool TextDocumentPdf::write(QTextDocument *document, const QPageLayout &layout,
                            const WriteGrant &outPdfGrant, QString *error,
                            const QList<DocumentBuilder::Decoration> &decorations)
{
    const QString outPdf = outPdfGrant.path();
    if (!document) return false;

    QPdfWriter writer(outPdf);
    writer.setResolution(DocumentBuilder::Resolution);
    writer.setPageLayout(layout);

    QPainter painter;
    if (!painter.begin(&writer)) {
        if (error) *error = tr("The PDF could not be written.");
        return false;
    }

    // Both lines have to come before anything asks for the page count: without
    // the paint device the document lays itself out against the screen.
    document->documentLayout()->setPaintDevice(&writer);
    document->setPageSize(writer.pageLayout()
                              .paintRectPixels(writer.resolution()).size());

    const QSizeF body  = document->pageSize();
    const int    pages = qMin(document->pageCount(), MaxPages);

    // Which page a fixed area belongs on is decided by the paragraph it was
    // anchored to, because that is the only thing that survives a reflow.
    const double toPixels = writer.resolution() / 72.0;
    const QPointF origin(layout.margins().left(), layout.margins().top());
    QHash<int, QList<const DocumentBuilder::Decoration *>> perPage;
    for (const DocumentBuilder::Decoration &decoration : decorations) {
        // A paragraph that holds nothing but shapes says nothing about where it
        // belongs: it is one point tall and lands on whichever side of a page
        // break the flow happens to leave it. The content it introduces decides
        // instead, which is the page the shapes were drawn for. That content
        // ends where the next page is forced to begin; with none before it,
        // the shapes stay with their own paragraph.
        const auto opensPage = [](const QTextBlock &b) {
            return b.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore;
        };
        QTextBlock anchor = document->findBlock(decoration.anchorPosition);
        if (decoration.afterPageBreak) {
            QTextBlock next = anchor.next();
            while (next.isValid() && !opensPage(next)) next = next.next();
            if (next.isValid()) anchor = next;
        }
        QTextBlock block = anchor;
        while (block.isValid() && block.text().trimmed().isEmpty()
               && block.next().isValid() && !opensPage(block.next()))
            block = block.next();
        if (block.text().trimmed().isEmpty()) block = anchor;

        const QRectF at = document->documentLayout()->blockBoundingRect(block);
        perPage[qBound(0, int(at.top() / body.height()), pages - 1)].append(&decoration);
    }

    // A line set tighter than its font reaches above its place, and the first
    // line of a page can reach back onto the page before. A line belongs to the
    // page that holds its middle; the page before stops just above it.
    const double edge = DocumentBuilder::pt2device(1.0);
    QList<double> pageEnds;
    for (int page = 0; page < pages; ++page) pageEnds.append((page + 1) * body.height() - edge);
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const QTextLayout *lines = block.layout();
        if (!lines) continue;
        const QPointF at = document->documentLayout()->blockBoundingRect(block).topLeft();
        for (int i = 0; i < lines->lineCount(); ++i) {
            const QRectF line = lines->lineAt(i).rect().translated(at);
            const int top = int(line.top() / body.height());
            if (top >= 0 && top < pages && int(line.center().y() / body.height()) > top)
                pageEnds[top] = qMin(pageEnds[top], line.top() - edge);
        }
    }

    for (int page = 0; page < pages; ++page) {
        if (page > 0) writer.newPage();

        for (const DocumentBuilder::Decoration *decoration : perPage.value(page)) {
            const QRectF area(
                (decoration->pageRectPt.x() - origin.x()) * toPixels,
                (decoration->pageRectPt.y() - origin.y()) * toPixels,
                decoration->pageRectPt.width()  * toPixels,
                decoration->pageRectPt.height() * toPixels);

            if (!decoration->image.isNull()) {
                painter.drawImage(area, decoration->image);
            } else if (decoration->fill.isValid()) {
                painter.fillRect(area, decoration->fill);
            }
            if (decoration->stroke.isValid()) {
                painter.setPen(QPen(decoration->stroke, toPixels));
                painter.drawRect(area);
            }
            if (!decoration->text.isEmpty()) {
                const TextStyle &style = decoration->textStyle;
                QFont font = document->defaultFont();
                if (style.family && !style.family->isEmpty()) font.setFamily(*style.family);
                if (style.sizePt && *style.sizePt > 0.0) font.setPointSizeF(*style.sizePt);
                font.setBold(style.bold.value_or(false));
                font.setItalic(style.italic.value_or(false));
                font.setUnderline(style.underline.value_or(false));
                painter.setFont(font);
                painter.setPen(style.color && style.color->isValid() ? *style.color
                                                                     : QColor(Qt::black));
                // The box is sized for the text it holds, so nothing is clipped
                // away and a line that runs long simply keeps going.
                painter.drawText(area, decoration->textAlign | Qt::TextDontClip,
                                 decoration->text);
            }
        }

        QAbstractTextDocumentLayout::PaintContext context;
        context.palette.setColor(QPalette::Text, Qt::black);
        // A paragraph is picked for a page when its rectangle touches it, and
        // that rectangle starts at its top margin. One that begins in the last
        // point of a page therefore gets painted here as well as on the page it
        // really belongs to, with its text landing in the bottom margin. The
        // last point is left out, where nothing can be visible anyway.
        context.clip = QRectF(0, page * body.height(),
                              body.width(), pageEnds[page] - page * body.height());

        painter.save();
        painter.translate(0, -page * body.height());
        document->documentLayout()->draw(&painter, context);
        painter.restore();
    }
    painter.end();

    if (QFileInfo(outPdf).size() <= 0) {
        if (error) *error = tr("The PDF could not be written.");
        return false;
    }
    return true;
}
