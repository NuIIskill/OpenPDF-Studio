#pragma once

#include <QString>

/// Everything that is not a PDF but can be turned into one before opening.
namespace DocumentImport {

/// Whether the file is a PDF, by its header and not by its name: a PDF that
/// carries no .pdf extension still opens straight away.
bool    isPdf(const QString &path);

bool    isSupported(const QString &path);
QString openFilter();
QString formatName(const QString &path);

/// textDumpPath, when given, gets the plain text of the parsed document before
/// the PDF is written. It separates a parser fault from a layout fault.
bool convertToPdf(const QString &path, const QString &outPdf,
                  QString *error = nullptr, const QString &textDumpPath = QString());

}
