#pragma once

#include "engine/export/DocxExporter.hpp"

/// Exports PDF content as an editable OpenDocument text file (.odt).
class OdtExporter
{
public:
    static bool exportToOdt(const QString &outputPath, const QList<DocxPage> &pages);
};
