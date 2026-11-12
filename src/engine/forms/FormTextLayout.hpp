#pragma once

#include <QFont>
#include <QList>
#include <QSizeF>
#include <QString>

/// Where PDFium puts the text of a text field: its font size, also when automatic, and its lines.
namespace FormTextLayout {

constexpr qreal kInset      = 1.0;
constexpr qreal kAscent     = 0.945;
constexpr qreal kLineHeight = 1.169;
constexpr qreal kLineSpacing = 7.0 / 6.0;

QFont font();
qreal textWidth(const QString &text, qreal size);
qreal fontSize(const QString &text, const QSizeF &box, bool multiline, qreal fixed);
QList<int> lineLengths(const QString &paragraph, qreal width, qreal size);
int lineCount(const QString &text, qreal width, qreal size);
qreal singleBaseline(qreal height, qreal size);
qreal firstBaseline(qreal size);

}
