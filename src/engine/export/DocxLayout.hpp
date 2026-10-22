#pragma once

#include "engine/export/DocxExporter.hpp"

#include <QImage>
#include <QList>
#include <QMarginsF>
#include <QSizeF>

struct DocxLayoutInput {
    QList<ContentItem> items;
    QImage             original;
    QImage             erased;
    QSizeF             pageSizePt;
    qreal              scale { 1.0 };
    DocxExportOptions  options;
};

QList<DocxBlock> buildDocxBlocks(const DocxLayoutInput &in, QMarginsF *marginsOut);
