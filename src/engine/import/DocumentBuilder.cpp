#include "engine/import/DocumentBuilder.hpp"

#include <QFont>
#include <QImage>
#include <QMap>
#include <QPageSize>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>
#include <QFontMetricsF>
#include <QTextTableCell>
#include <QUrl>

namespace {

constexpr double DefaultPageWidthPt  = 595.0;
constexpr double DefaultPageHeightPt = 842.0;
constexpr double DefaultMarginPt     = 56.7;   // 2 cm

/// Qt drops a border thinner than one unit, so a hairline still has to ask for
/// a whole one. Measured, and pinned by .claude/testing/builder_units_smoke.cpp.
qreal borderWidth(double points)
{
    if (points <= 0.0) return 0.0;
    return qMax(1.0, DocumentBuilder::pt2scaled(points));
}

/// Ligatures are turned off for the whole document. Qt writes a ligature into
/// the PDF as one glyph without a character mapping behind it, so "starting"
/// comes back out of the file as "star" and "ng": unsearchable, and wrong as
/// soon as anyone copies it. One glyph per character is worth more here than
/// the typography.
const QMap<QFont::Tag, quint32> &noLigatures()
{
    static const QMap<QFont::Tag, quint32> features {
        { QFont::Tag("liga"), 0 },
        { QFont::Tag("clig"), 0 },
        { QFont::Tag("dlig"), 0 },
        { QFont::Tag("hlig"), 0 },
    };
    return features;
}

struct TableState {
    QTextTable *table           { nullptr };
    int         columns         { 0 };
    int         row             { -1 };
    int         column          { -1 };
    double      rowMinimumPt    { 0.0 };
    bool        cellNeedsHeight { false };
};

}

struct DocumentBuilder::Private {
    std::unique_ptr<QTextDocument> document;
    std::unique_ptr<QTextCursor>   cursor;

    QSizeF    pageSizePt { DefaultPageWidthPt, DefaultPageHeightPt };
    QMarginsF marginsPt  { DefaultMarginPt, DefaultMarginPt,
                           DefaultMarginPt, DefaultMarginPt };

    /// The cursor sits on an empty block that the next paragraph should take
    /// over instead of inserting one of its own.
    bool emptyBlockReady { true };
    bool breakBeforeNext { false };

    QList<Decoration>   decorations;
    QList<TableState>   tables;
    QList<QTextList *>  lists;
    std::optional<ListStyle> currentList;
    int imageCount { 0 };
};

DocumentBuilder::DocumentBuilder() : d(std::make_unique<Private>())
{
    d->document = std::make_unique<QTextDocument>();
    d->document->setDocumentMargin(0);
    d->document->setUndoRedoEnabled(false);
    d->cursor = std::make_unique<QTextCursor>(d->document.get());
}

DocumentBuilder::~DocumentBuilder() = default;

QTextDocument *DocumentBuilder::document() const { return d->document.get(); }

QPageLayout DocumentBuilder::pageLayout() const
{
    return QPageLayout(QPageSize(d->pageSizePt, QPageSize::Point, QString(),
                                 QPageSize::ExactMatch),
                       QPageLayout::Portrait, d->marginsPt, QPageLayout::Point);
}

void DocumentBuilder::setPageSizePt(const QSizeF &size)
{
    if (size.width() > 1.0 && size.height() > 1.0) d->pageSizePt = size;
}

void DocumentBuilder::setMarginsPt(const QMarginsF &margins)
{
    d->marginsPt = margins;
}

void DocumentBuilder::setDefaultFont(const QString &family, double sizePt)
{
    QFont font = d->document->defaultFont();
    if (!family.isEmpty()) font.setFamily(family);
    if (sizePt > 0.0)      font.setPointSizeF(sizePt);
    font.setStyleHint(QFont::SansSerif);
    for (auto it = noLigatures().cbegin(); it != noLigatures().cend(); ++it)
        font.setFeature(it.key(), it.value());
    d->document->setDefaultFont(font);
}

void DocumentBuilder::startParagraph(const BlockStyle &block,
                                     const std::optional<ListStyle> &list)
{
    QTextBlockFormat format;
    if (block.alignment)         format.setAlignment(*block.alignment);
    if (block.leftIndentPt)      format.setLeftMargin(pt2device(*block.leftIndentPt));
    if (block.rightIndentPt)     format.setRightMargin(pt2device(*block.rightIndentPt));
    if (block.firstLineIndentPt) format.setTextIndent(pt2device(*block.firstLineIndentPt));
    if (block.spaceBeforePt)     format.setTopMargin(pt2scaled(*block.spaceBeforePt));
    if (block.spaceAfterPt)      format.setBottomMargin(pt2scaled(*block.spaceAfterPt));
    if (block.background)        format.setBackground(*block.background);
    if (block.headingLevel)      format.setHeadingLevel(*block.headingLevel);
    if (block.lineHeightExactPt)
        format.setLineHeight(pt2scaled(*block.lineHeightExactPt),
                             QTextBlockFormat::FixedHeight);
    else if (block.lineHeightMinimumPt)
        format.setLineHeight(pt2scaled(*block.lineHeightMinimumPt),
                             QTextBlockFormat::MinimumHeight);
    else if (block.lineHeightPercent)
        format.setLineHeight(*block.lineHeightPercent, QTextBlockFormat::ProportionalHeight);
    if (block.pageBreakBefore.value_or(false)) d->breakBeforeNext = true;
    if (d->breakBeforeNext) {
        format.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        d->breakBeforeNext = false;
    }

    // The row's minimum height becomes padding under the cell's first line, now
    // that the line's own height is known.
    if (!d->tables.isEmpty() && d->tables.last().cellNeedsHeight) {
        TableState &state = d->tables.last();
        state.cellNeedsHeight = false;

        const double lineHeight = block.lineHeightExactPt.value_or(
            QFontMetricsF(d->document->defaultFont()).height() * 72.0 / 96.0);
        const double missing = state.rowMinimumPt - lineHeight;
        if (missing > 0.0) {
            QTextTableCell cell = state.table->cellAt(state.row, state.column);
            QTextTableCellFormat format = cell.format().toTableCellFormat();
            format.setBottomPadding(format.bottomPadding() + pt2scaled(missing));
            cell.setFormat(format);
        }
    }

    if (d->emptyBlockReady) {
        // The block character format is left alone: inside a table cell Qt keeps
        // the cell's own format there, and resetting it strips the shading and
        // the borders off the cell again.
        d->cursor->setBlockFormat(format);
        d->emptyBlockReady = false;
    } else {
        d->cursor->insertBlock(format, QTextCharFormat());
    }

    if (list) {
        if (!d->currentList || !(*d->currentList == *list)
                || list->level >= d->lists.size() || !d->lists.value(list->level)) {
            QTextListFormat listFormat;
            listFormat.setStyle(list->numbered ? QTextListFormat::ListDecimal
                                               : QTextListFormat::ListDisc);
            listFormat.setIndent(list->level + 1);
            while (d->lists.size() <= list->level) d->lists.append(nullptr);
            d->lists[list->level] = d->cursor->createList(listFormat);
        } else {
            d->lists[list->level]->add(d->cursor->block());
        }
        d->currentList = list;
    } else {
        d->currentList.reset();
        d->lists.clear();
    }
}

void DocumentBuilder::appendText(const QString &text, const TextStyle &style)
{
    if (text.isEmpty()) return;

    QTextCharFormat format;
    format.setFontFeatures(noLigatures());
    if (style.bold)      format.setFontWeight(*style.bold ? QFont::Bold : QFont::Normal);
    if (style.italic)    format.setFontItalic(*style.italic);
    if (style.underline) format.setFontUnderline(*style.underline);
    if (style.strikeOut) format.setFontStrikeOut(*style.strikeOut);
    if (style.sizePt && *style.sizePt > 0.0) format.setFontPointSize(*style.sizePt);
    if (style.family && !style.family->isEmpty()) {
        format.setFontFamilies({ *style.family });
        // Where the document's font is not installed, the fallback should still
        // be a proportional face. Without the hint a missing Calibri can end up
        // as a monospace font, which is never what the document meant.
        format.setFontStyleHint(QFont::SansSerif);
    }
    if (style.color && style.color->isValid()) format.setForeground(*style.color);
    if (style.verticalAlign)
        format.setVerticalAlignment(
            static_cast<QTextCharFormat::VerticalAlignment>(*style.verticalAlign));

    d->cursor->insertText(text, format);
    d->emptyBlockReady = false;
}

void DocumentBuilder::appendLineBreak(const TextStyle &style)
{
    appendText(QString(QChar::LineSeparator), style);
}

void DocumentBuilder::appendPageBreak()
{
    d->breakBeforeNext = true;
}

void DocumentBuilder::appendImage(const QImage &image, const QSizeF &sizePt)
{
    if (image.isNull()) return;

    const QString name = QStringLiteral("import://image%1").arg(++d->imageCount);
    d->document->addResource(QTextDocument::ImageResource, QUrl(name), image);

    QSizeF size = sizePt;
    if (size.width() <= 1.0 || size.height() <= 1.0)
        size = QSizeF(image.width() * 72.0 / 96.0, image.height() * 72.0 / 96.0);

    QTextImageFormat format;
    format.setName(name);
    format.setWidth(pt2scaled(size.width()));
    format.setHeight(pt2scaled(size.height()));
    d->cursor->insertImage(format);
    d->emptyBlockReady = false;
}

void DocumentBuilder::addDecoration(Decoration decoration)
{
    if (decoration.pageRectPt.isEmpty()) return;
    if (!decoration.fill.isValid() && decoration.image.isNull()
            && decoration.text.isEmpty() && !decoration.stroke.isValid())
        return;
    decoration.anchorPosition = d->cursor->position();
    d->decorations.append(std::move(decoration));
}

void DocumentBuilder::markPageStart()
{
    if (d->cursor->block().position() <= 0) return;   // page one starts by itself
    if (!d->tables.isEmpty()) return;                 // not inside a table

    QTextBlockFormat format;
    format.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    // No space above a paragraph that opens a page: the margin would reach back
    // onto the page before it.
    format.setTopMargin(0);
    d->cursor->mergeBlockFormat(format);
}

const QList<DocumentBuilder::Decoration> &DocumentBuilder::decorations() const
{
    return d->decorations;
}

void DocumentBuilder::startTable(const QList<double> &columnWidthsPt,
                                const TableStyle &style)
{
    const int columns = qMax(1, static_cast<int>(columnWidthsPt.size()));

    QTextTableFormat format;
    format.setCellPadding(pt2scaled(2.0));
    format.setCellSpacing(0);
    format.setBorderCollapse(true);
    format.setBorder(borderWidth(style.borderWidthPt.value_or(0.5)));
    format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    if (style.borderColor && style.borderColor->isValid())
        format.setBorderBrush(*style.borderColor);

    // Column widths go in as percentages: those need no unit and so cannot be
    // caught out by the two different length scales in this document.
    double total = 0.0;
    for (double width : columnWidthsPt) total += qMax(0.0, width);
    if (total > 1.0) {
        QList<QTextLength> constraints;
        constraints.reserve(columns);
        for (double width : columnWidthsPt)
            constraints.append(QTextLength(QTextLength::PercentageLength,
                                           qMax(0.0, width) / total * 100.0));
        format.setColumnWidthConstraints(constraints);

        const double textWidth = d->pageSizePt.width()
                               - d->marginsPt.left() - d->marginsPt.right();
        if (textWidth > 1.0)
            format.setWidth(QTextLength(QTextLength::PercentageLength,
                                        qBound(5.0, total / textWidth * 100.0, 100.0)));
    }

    TableState state;
    state.columns = columns;
    state.table   = d->cursor->insertTable(1, columns, format);
    d->tables.append(state);
    d->emptyBlockReady = false;
}

void DocumentBuilder::startTableRow(double minimumHeightPt)
{
    if (d->tables.isEmpty()) return;
    TableState &state = d->tables.last();
    ++state.row;
    if (state.row > 0) state.table->appendRows(1);
    state.column       = -1;
    state.rowMinimumPt = minimumHeightPt;
}

void DocumentBuilder::startTableCell(int span, const CellStyle &style)
{
    if (d->tables.isEmpty()) return;
    TableState &state = d->tables.last();
    if (state.row < 0) startTableRow();

    state.column = qMin(state.column + 1, state.columns - 1);
    if (span > 1 && state.column + span <= state.columns)
        state.table->mergeCells(state.row, state.column, 1, span);

    QTextTableCell cell = state.table->cellAt(state.row, state.column);
    if (style.background || style.borderColor || style.borderWidthPt
            || style.paddingPt) {
        QTextTableCellFormat format = cell.format().toTableCellFormat();
        if (style.background && style.background->isValid())
            format.setBackground(*style.background);

        // Word states the inset per side, and a table of several rows drifts
        // visibly down the page if it is guessed instead of read.
        if (style.paddingPt) {
            format.setLeftPadding(pt2scaled(style.paddingPt->left()));
            format.setTopPadding(pt2scaled(style.paddingPt->top()));
            format.setRightPadding(pt2scaled(style.paddingPt->right()));
            format.setBottomPadding(pt2scaled(style.paddingPt->bottom()));
        }

        // With collapsing borders the grid comes from the cells, so a cell that
        // states its own border has to state all four sides of it.
        if (style.borderWidthPt || style.borderColor) {
            const qreal width = borderWidth(style.borderWidthPt.value_or(0.5));
            const QBrush brush = style.borderColor && style.borderColor->isValid()
                ? QBrush(*style.borderColor) : QBrush(Qt::darkGray);
            const auto edge = width > 0.0 ? QTextFrameFormat::BorderStyle_Solid
                                          : QTextFrameFormat::BorderStyle_None;
            format.setTopBorder(width);    format.setTopBorderStyle(edge);
            format.setLeftBorder(width);   format.setLeftBorderStyle(edge);
            format.setRightBorder(width);  format.setRightBorderStyle(edge);
            format.setBottomBorder(width); format.setBottomBorderStyle(edge);
            format.setTopBorderBrush(brush);    format.setLeftBorderBrush(brush);
            format.setRightBorderBrush(brush);  format.setBottomBorderBrush(brush);
        }
        cell.setFormat(format);
    }

    state.cellNeedsHeight = state.rowMinimumPt > 0.0;

    *d->cursor = cell.firstCursorPosition();
    d->emptyBlockReady = true;
    if (span > 1) state.column += span - 1;
}

void DocumentBuilder::endTable()
{
    if (d->tables.isEmpty()) return;
    QTextTable *table = d->tables.takeLast().table;
    d->cursor->setPosition(table->lastPosition() + 1);
    d->emptyBlockReady = false;
    d->currentList.reset();
    d->lists.clear();
}

bool DocumentBuilder::inTable() const { return !d->tables.isEmpty(); }
