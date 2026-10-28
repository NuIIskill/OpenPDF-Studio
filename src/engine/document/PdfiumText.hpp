#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/edit/EditSession.hpp"
#include "engine/edit/TextLayout.hpp"

#include "fpdfview.h"

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <memory>
#include <vector>

class PdfiumPageSpace;

/// Sets edited text on a PDFium page in the styles of the characters it came from.
namespace PdfiumText {

struct Origin {
    FPDF_FONT font   { nullptr };
    double    sizePt { 0.0 };
    QString   text;
};

Origin originOf(const QList<FPDF_PAGEOBJECT> &objects, FPDF_TEXTPAGE textPage);
QString objectText(FPDF_PAGEOBJECT obj, FPDF_TEXTPAGE textPage);

class FontChoice;

class Typeset
{
public:
    Typeset(FPDF_DOCUMENT doc, FPDF_PAGE page, FPDF_TEXTPAGE textPage,
            const EditSession::Edit &edit, const Origin *origin, bool laidOut = true,
            const QHash<int, FPDF_PAGE> *sourcePages = nullptr, bool keepObjects = true);
    ~Typeset();
    Typeset(const Typeset &) = delete;
    Typeset &operator=(const Typeset &) = delete;

    bool anchored() const { return m_layout.anchored; }
    QList<FPDF_PAGEOBJECT> replaces(const QList<FPDF_PAGEOBJECT> &objects,
                                    const PdfiumPageSpace &space) const;
    void insertInto(FPDF_PAGE page, const PdfiumPageSpace &space) const;
    TextLayout::Metrics metrics() const;
    QList<TextLayout::OriginalLine> laidLines(QList<QList<TextLayout::Source>> *sources,
                                              QList<QList<TextLayout::Mark>> *marks = nullptr) const;

private:
    struct Style {
        FPDF_FONT font { nullptr };
        double    size { 0.0 };
        double    a { 1.0 };
        double    c { 0.0 };
        QColor    fill;
        QColor    stroke;
        double    strokeWidth { 0.0 };
        int       mode { 0 };
        double    track { 0.0 };
        double    rise { 0.0 };
        QString   known;
        QString   family;
    };
    struct Placed {
        char32_t codePoint;
        double   x;
        int      style;
    };

    static Style styleOf(FPDF_PAGEOBJECT obj, const PdfiumPageSpace &space);
    static bool sameStyle(const Style &x, const Style &y);
    int addStyle(const Style &s);
    void readStyles(const QList<FPDF_PAGEOBJECT> &sources, const QList<double> &rise);
    void addSourceStyles();
    void findStyleSources();
    void addDefaultStyle(const Origin *origin);
    void applyOverrides();
    void applyMarks();
    bool restyle(Style &s, const EditSession::Edit &e);
    bool underlinedAt(int position) const;
    bool writable(FPDF_FONT font) const;
    FPDF_FONT standardFont(const QByteArray &name);
    double advance(int style, char32_t codePoint) const;
    int styleAt(int position) const;
    TextLayout::Advance advanceFunction() const;
    void insertRuns(FPDF_PAGE page, const QList<Placed> &glyphs, double baseline,
                    const PdfiumPageSpace &space) const;
    void insertRun(FPDF_PAGE page, int style, FPDF_FONT font, const QList<uint> &codePoints,
                   const QList<double> &x, double baseline, const PdfiumPageSpace &space) const;

    FPDF_DOCUMENT     m_doc;
    FPDF_PAGE         m_page;
    FPDF_TEXTPAGE     m_textPage;
    EditSession::Edit m_edit;
    QList<TextLayout::Source> m_sources;
    QList<int>        m_sourceStyles;
    QList<TextLayout::Source> m_styleSources;
    const QHash<int, FPDF_PAGE> *m_givenPages { nullptr };
    std::vector<std::pair<FPDF_PAGE, FPDF_TEXTPAGE>> m_sourcePages;
    QString           m_text;
    QString           m_original;
    QList<Style>      m_styles;
    QList<FPDF_FONT>  m_ownFonts;
    QList<int>        m_originalStyles;
    QList<FPDF_PAGEOBJECT> m_sourceObjects;
    QList<int>        m_charStyles;
    QList<int>        m_unmarked;
    QList<TextLayout::Mark> m_charMarks;
    QList<TextLayout::Mark> m_marks;
    QList<QList<int>> m_markStyles;
    std::vector<std::unique_ptr<FontChoice>> m_fonts;
    std::vector<FPDF_FONT> m_owned;
    QHash<QByteArray, QSet<FPDF_FONT>> m_fontNames;
    double            m_size { 0.0 };
    bool              m_mapped { false };
    bool              m_anchor { false };
    bool              m_keep { false };
    bool              m_restyled { false };
    TextLayout::Result m_layout;
};

}

#endif
