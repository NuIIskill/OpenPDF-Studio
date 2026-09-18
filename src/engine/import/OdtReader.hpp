#pragma once

#include <QString>

class DocumentBuilder;
class ZipArchive;

/// Reads content.xml of an odt into a DocumentBuilder.
namespace OdtReader {

bool read(const ZipArchive &zip, DocumentBuilder &builder, QString *error = nullptr);

}
