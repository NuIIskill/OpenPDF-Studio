#pragma once

#include "engine/edit/DocxExporter.hpp"

#include <QImage>
#include <QList>
#include <QMarginsF>
#include <QSizeF>

// ── Page layout analysis ──────────────────────────────────────────────────────
// Turns the flat per-line ContentItem list into the structured block model the
// DOCX writer serialises: real paragraphs, real tables, shaded/bordered boxes,
// and pictures for the parts a word processor cannot express (charts, vector
// art, logos).
//
// The raster is consulted only for things the content model cannot answer —
// where a fill really ends, whether a box has a border, and what is left on the
// page once every recognised text run has been painted out.
struct DocxLayoutInput {
    QList<ContentItem> items;
    QImage             original;    // page as rendered, text included
    QImage             erased;      // same page with recognised text painted out
    QSizeF             pageSizePt;
    qreal              scale { 1.0 }; // raster pixels per PDF point
};

// Returns blocks in reading order. marginsOut receives page margins derived
// from the content extent. An empty result means the page had no usable
// structure and the caller should fall back to the positioned export.
QList<DocxBlock> buildDocxBlocks(const DocxLayoutInput &in, QMarginsF *marginsOut);
