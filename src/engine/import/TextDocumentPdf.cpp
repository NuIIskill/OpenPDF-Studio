#include "engine/import/TextDocumentPdf.hpp"

#include "engine/import/DocumentBuilder.hpp"

#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QFileInfo>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QHash>
#include <QFont>
#include <QPen>
#include <QTextBlock>
#include <QTextDocument>

namespace {

constexpr int MaxPages = 5000;

QString tr(const char *text) { return QCoreApplication::translate("DocumentImport", text); }

}

// QTextDocument::print is deliberately not used: it lays out on a clone, and
// whether the images registered as resources survive that clone depends on the
// Qt version. The loop below is what print does anyway, minus the guessing.
bool TextDocumentPdf::write(QTextDocument *document, const QPageLayout &layout,
                            const QString &outPdf, QString *error,
                            const QList<DocumentBuilder::Decoration> &decorations)
{
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
        // instead, which is the page the shapes were drawn for.
        QTextBlock block = document->findBlock(decoration.anchorPosition);
        while (block.isValid() && block.text().trimmed().isEmpty()
               && block.next().isValid())
            block = block.next();

        const QRectF at = document->documentLayout()->blockBoundingRect(block);
        perPage[qBound(0, int(at.top() / body.height()), pages - 1)].append(&decoration);
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
        const double edge = DocumentBuilder::pt2device(1.0);
        context.clip = QRectF(0, page * body.height(),
                              body.width(), body.height() - edge);

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
