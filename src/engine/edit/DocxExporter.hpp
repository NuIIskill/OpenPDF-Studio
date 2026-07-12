#pragma once

#include "ContentMap.hpp"

#include <QImage>
#include <QList>
#include <QSizeF>
#include <QString>

struct DocxPage {
    QSizeF pageSizePt;
    QList<ContentItem> items;
    QImage background; // rendered non-text PDF content (images, rules, graphics)
};

// Exports positioned PDF content as an editable .docx file.
// Pure C++/Qt — no external tools or libraries required.
// Uses ZIP store compression (method=0), accepted by all Word/LibreOffice versions.
class DocxExporter
{
public:
    static bool exportToDocx(const QString &outputPath,
                             const QList<DocxPage> &pages,
                             const QString &title = {});

    // Compatibility entry point for callers which have no page geometry.
    static bool exportToDocx(const QString       &outputPath,
                             const QList<QString> &pageTexts,
                             const QString        &title = {});
};
