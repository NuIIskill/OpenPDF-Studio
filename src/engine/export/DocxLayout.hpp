#pragma once

#include "engine/export/DocxExporter.hpp"

#include <QImage>
#include <QList>
#include <QMarginsF>
#include <QSizeF>

/// An image object of the page: where it shows, and its own pixels.
struct DocxSourceImage {
    QRectF bounds;
    QImage image;
};

struct DocxLayoutInput {
    QList<ContentItem> items;
    QList<DocxSourceImage> images;
    QImage             original;
    QImage             erased;
    QSizeF             pageSizePt;
    qreal              scale { 1.0 };
    DocxExportOptions  options;
};

QList<DocxBlock> buildDocxBlocks(const DocxLayoutInput &in, QMarginsF *marginsOut);
