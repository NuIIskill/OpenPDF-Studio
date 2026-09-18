#pragma once

#include <QString>

class DocumentBuilder;
class ZipArchive;

/// Reads word/document.xml of a docx into a DocumentBuilder.
namespace DocxReader {

bool read(const ZipArchive &zip, DocumentBuilder &builder, QString *error = nullptr);

}
