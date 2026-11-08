#pragma once

#include <QStringList>

class QPDF;

/// Turns a PDF held by qpdf into PDF/A-2b, or names what stands in the way.
namespace PdfA {

struct Report {
    QStringList blockers;
    QStringList changes;
    int         repairs { 0 };

    bool clean() const { return blockers.isEmpty() && changes.isEmpty() && repairs == 0; }
};

void convert(QPDF &pdf, Report &report);

}
