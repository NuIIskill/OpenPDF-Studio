#pragma once

#ifdef HAVE_QPDF

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <map>
#include <set>
#include <string>

/// What the content streams of a PDF draw with: its fonts and the characters shown, and CMYK colour.
namespace PdfAContent {

struct FontUse {
    QPDFObjectHandle      font;
    std::set<int>         codes;
    std::set<std::string> strings;
    bool                  visible { false };
};

struct Use {
    std::map<QPDFObjGen, FontUse> fonts;
    std::set<int>                 cmykPages;
};

Use scan(QPDF &pdf);

}

#endif
