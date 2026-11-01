#pragma once

#include "engine/export/DocxExporter.hpp"

/// Exports PDF content as flowing Markdown or HTML, with headings, lists and tables.
class MarkupExporter
{
public:
    static bool exportToMarkdown(const QString &outputPath, const QList<DocxPage> &pages);
    static bool exportToHtml(const QString &outputPath, const QList<DocxPage> &pages,
                             const QString &title = {});
};
