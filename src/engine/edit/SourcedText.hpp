#pragma once

#include "engine/edit/TextLayout.hpp"

#include <QList>
#include <QSizeF>
#include <QString>

QT_BEGIN_NAMESPACE
class QMimeData;
QT_END_NAMESPACE

/// Copied text with, for each character, the document character whose style it has.
namespace SourcedText {

/// A box copied whole: its size and where each character is set, relative to its top left.
struct Box {
    QSizeF size;
    double fontSizePt { 0.0 };
    QList<TextLayout::OriginalLine>   lines;
    QList<QList<TextLayout::Source>>  sources;
};

QString documentKey(const void *document);
void write(QMimeData *mime, const QString &document, const QList<TextLayout::Source> &sources,
           const Box &box = {});
QList<TextLayout::Source> read(const QMimeData *mime, const QString &document);
Box box(const QMimeData *mime, const QString &document);
Box boxAround(QList<TextLayout::OriginalLine> lines, const QList<QList<TextLayout::Source>> &sources,
              double fontSizePt);

}
