#pragma once

#include "engine/export/DocxExporter.hpp"

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QList>
#include <QString>

/// Stores one Word media image and its relationship ID.
struct MediaPart {
    QString    name;
    QString    relId;
    QByteArray png;
};

/// Provides WordprocessingML serialization helpers.
namespace DocxXml {

/// A stretch of a paragraph's text in one look, taken from the item it carries.
struct Span {
    QString     text;
    ContentItem style;
};


QString emu(double pt);
QString twips(double pt);
bool encodePng(const QImage &image, QByteArray *out);
QString xmlEsc(const QString &s);
QString withoutSymbolFont(const QString &text);
bool isSymbolFont(const QString &family);
QString colorHex(const QColor &color);
double fontSizeOf_(const ContentItem &item);
QString textRuns(const ContentItem &item);
QString semanticTextRuns(const ContentItem &item);
QString paragraphText(const QList<ContentItem> &lines);
QList<Span> paragraphSpans(const DocxBlock &block);
QList<double> tabStops(const DocxBlock &block);
QString runProperties(const ContentItem &style);
QString alignValue(Qt::Alignment align);
int linePitchTwips(const DocxBlock &block);
double flowTop(const DocxBlock &block);
double cellLead(const DocxBlock &table, int row, const ContentItem &item);
QString tableXml(const DocxBlock &block);
QString pictureXml(const DocxBlock &block, const QString &relId, int id);
QString shapeXml(const DocxBlock &block, int id);
QString textBoxXml(const DocxBlock &block, int id);
QRectF textBoxRect(const ContentItem &item, const QRectF &fallback, double *lineHeightPt);
DocxImage encodePicture(const QImage &image, const DocxExportOptions &opt);
QString semanticParagraph(const ContentItem &item, int beforeTwips, int leftTwips = 0);
QString paragraphXml(const DocxBlock &block, double spaceBeforePt, bool insideCell);

}
