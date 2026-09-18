#pragma once

#include <QString>
#include <QStringList>

/// Turns one image file into a PDF of a single page the size of the image.
namespace ImageImport {

QStringList suffixes();
bool        isSupported(const QString &path);
QString     nameFilter();

bool toPdf(const QString &imagePath, const QString &outPdf, QString *error = nullptr);

}
