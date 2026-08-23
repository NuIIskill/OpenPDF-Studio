#pragma once

#include "engine/edit/DocxExporter.hpp"

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QList>
#include <QString>

// One PNG inside word/media, already paired with the relationship id the
// document body refers to.
struct MediaPart {
    QString    name;     // file name inside word/media
    QString    relId;
    QByteArray png;
};

// WordprocessingML primitives: unit conversion, escaping, image encoding, and
// the fragments a DocxBlock turns into. Assembling whole documents from them
// is DocxExporter's job.
namespace DocxXml {

QString emu(double pt);
QString twips(double pt);
bool encodePng(const QImage &image, QByteArray *out);
QString xmlEsc(const QString &s);
QString colorHex(const QColor &color);
double fontSizeOf_(const ContentItem &item);
QString textRuns(const ContentItem &item);
QString semanticTextRuns(const ContentItem &item);
QString paragraphText(const QList<ContentItem> &lines);
QString runProperties(const ContentItem &style);
QString alignValue(Qt::Alignment align);
int linePitchTwips(const DocxBlock &block);
QString tableXml(const DocxBlock &block);
QString pictureXml(const DocxBlock &block, const QString &relId, int id);
QString shapeXml(const DocxBlock &block, int id);
QString textBoxXml(const DocxBlock &block, int id);
QString encodePicture(const QImage &image, const DocxExportOptions &opt, QByteArray *out);
QString semanticParagraph(const ContentItem &item, int beforeTwips, int leftTwips = 0);
QString paragraphXml(const DocxBlock &block, double spaceBeforePt, bool insideCell);

}   // namespace DocxXml
