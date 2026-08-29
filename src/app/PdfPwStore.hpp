#pragma once

#include <QString>
#include <string>

/// Holds the passwords the user typed for encrypted documents, keyed by absolute file path, for the lifetime of the process only.
namespace PdfPwStore {

void    set(const QString &filePath, const QString &pw);
QString get(const QString &filePath);
bool    has(const QString &filePath);
void    forget(const QString &filePath);
void    clear();

std::string forQpdf(const QString &filePath);

}
