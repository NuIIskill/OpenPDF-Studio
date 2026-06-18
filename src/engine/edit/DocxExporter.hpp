#pragma once

#include <QList>
#include <QString>

// Exports a list of per-page strings as a minimal .docx file.
// Pure C++/Qt — no external tools or libraries required.
// Uses ZIP store compression (method=0), accepted by all Word/LibreOffice versions.
class DocxExporter
{
public:
    static bool exportToDocx(const QString       &outputPath,
                             const QList<QString> &pageTexts,
                             const QString        &title = {});
};
