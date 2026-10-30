#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/WriteGrant.hpp"

#include "fpdfview.h"

#include <QList>
#include <QString>

#include <functional>

class EditSession;

namespace PdfiumWriter {

struct PagePass {
    QList<int> pages;
    std::function<void(FPDF_DOCUMENT doc)> begin;
    std::function<bool(FPDF_DOCUMENT doc, FPDF_PAGE page, int pageIndex, bool &inPlace)> change;
    std::function<void()> end;
};

bool save(const QString &sourcePath, const WriteGrant &output,
          const EditSession &session, const std::function<bool()> &cancelled = {});

bool rewrite(const QString &sourcePath, const WriteGrant &output, const PagePass &pass,
             const std::function<bool()> &cancelled = {});

}

#endif
