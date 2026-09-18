#pragma once

#include "engine/import/OfficeStyle.hpp"

#include <QColor>
#include <QImage>
#include <QList>
#include <QMarginsF>
#include <QRectF>
#include <QPageLayout>
#include <QSizeF>
#include <QString>

#include <memory>
#include <optional>

QT_BEGIN_NAMESPACE
class QTextCursor;
class QTextDocument;
class QTextTable;
QT_END_NAMESPACE

/// Builds the QTextDocument that a docx or odt turns into.
///
/// Lengths handed in are always points; everything the document itself stores is
/// in device pixels of the PDF that will be written from it.
class DocumentBuilder
{
public:
    DocumentBuilder();
    ~DocumentBuilder();

    QTextDocument *document() const;
    QPageLayout    pageLayout() const;

    void setPageSizePt(const QSizeF &size);
    void setMarginsPt(const QMarginsF &margins);
    void setDefaultFont(const QString &family, double sizePt);

    void startParagraph(const BlockStyle &block, const std::optional<ListStyle> &list = {});
    void appendText(const QString &text, const TextStyle &style);
    void appendLineBreak(const TextStyle &style);
    void appendPageBreak();
    void appendImage(const QImage &image, const QSizeF &sizePt);

    /// One width in points per column; a zero width leaves that column to the
    /// layout. An empty list sizes the whole table by its content.
    void startTable(const QList<double> &columnWidthsPt, const TableStyle &style = {});
    /// Word states a minimum height per row, which a Qt table has no notion of.
    /// It is turned into padding under the first line of each cell.
    void startTableRow(double minimumHeightPt = 0.0);
    void startTableCell(int span, const CellStyle &cell = {});
    void endTable();
    bool inTable() const;

    /// A filled area or picture that belongs at a fixed spot on the page rather
    /// than in the text flow. The anchor names the position in the text it hung
    /// on, which is what decides the page it lands on.
    struct Decoration {
        QRectF        pageRectPt;
        QColor        fill;
        QColor        stroke;
        QImage        image;
        QString       text;
        TextStyle     textStyle;
        Qt::Alignment textAlign { Qt::AlignLeft | Qt::AlignTop };
        int           anchorPosition { 0 };
    };

    void addDecoration(Decoration decoration);

    /// The current paragraph begins a page. A band that starts at the very top
    /// of the page says so about the paragraph it hangs on, and without it the
    /// page break can fall one paragraph late.
    void markPageStart();
    const QList<Decoration> &decorations() const;

    /// The document is laid out for a device of this resolution, so its lengths
    /// are in pixels of that device while fonts stay in points.
    static constexpr int Resolution = 1200;

    /// Lengths the layout uses as they are: left and right margins, and the
    /// first line indent.
    static double pt2device(double points) { return points * Resolution / 72.0; }

    /// Lengths the layout scales itself, by the device resolution over 96: top
    /// and bottom margins, cell padding and image sizes. Measured, not assumed,
    /// and pinned by .claude/testing/builder_units_smoke.cpp.
    static double pt2scaled(double points) { return points * 96.0 / 72.0; }

private:
    struct Private;
    std::unique_ptr<Private> d;
};
