#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include <QString>

class EditSession;

namespace PdfiumWriter {

bool save(const QString &sourcePath, const QString &outputPath,
          const EditSession &session);

}

#endif
