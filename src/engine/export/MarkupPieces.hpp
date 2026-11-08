#pragma once

#include "engine/export/DocxExporter.hpp"

#include <QColor>
#include <QImage>
#include <QList>
#include <QRectF>
#include <QString>

/// The flowing document the Markdown and HTML exports are written from.
namespace MarkupPieces {

struct Inline {
    QString text;
    bool    bold   { false };
    bool    italic { false };
};

/// One piece of the flowing document: a paragraph, heading, list item, table, picture or box.
struct Piece {
    enum class Kind { Paragraph, Heading, Item, Table, Picture, Box };

    Kind          kind { Kind::Paragraph };
    int           level { 0 };
    bool          ordered { false };
    int           number { 1 };
    QList<Inline> text;
    Qt::Alignment align { Qt::AlignLeft };
    DocxTable     table;
    DocxImage     picture;
    DocxImage     withLabels;
    double        widthPt { 0.0 };
    QRectF        bounds;
    double        sizePt { 0.0 };
    QColor        color;
    bool          label { false };
    bool          subtitle { false };
    int           page { 0 };
    int           lastPage { 0 };
};

/// A link over part of a text, by its place in the text's plain form.
struct Anchor {
    qsizetype start { 0 };
    qsizetype end   { 0 };
    QString   url;
};

/// Consecutive parts of a text under the same anchor, or under none (-1).
struct Run {
    QList<Inline> text;
    int           anchor { -1 };
};

QString plain(const QList<Inline> &text);
double bodySize(const QList<DocxPage> &pages);
QList<Piece> piecesOf(const QList<DocxPage> &pages, QList<Piece> *boxes = nullptr);
QList<Inline> cellText(const DocxCell &cell);
QList<QList<const DocxCell *>> grid(const DocxTable &table);
bool write(const QString &path, const QString &text);

QList<Anchor> anchorsIn(const QList<Inline> &text, QList<QList<DocxLink>> &links,
                        int firstPage, int lastPage);
QList<Run> runsOf(const QList<Inline> &text, const QList<Anchor> &anchors);
bool boldRow(const QList<const DocxCell *> &row);
QImage decoded(const DocxImage &picture);
bool positioned(const Piece &piece);
void dropBackdrops(QList<Piece> &pieces);
void dropRunning(QList<Piece> &pieces, const QList<DocxPage> &pages);
void markSubtitles(QList<Piece> &pieces);
void closeHeadingLevels(QList<Piece> &pieces);
QList<int> boxesOf(const QList<Piece> &pieces, const QList<Piece> &boxes,
                   const QList<DocxPage> &pages);

}
