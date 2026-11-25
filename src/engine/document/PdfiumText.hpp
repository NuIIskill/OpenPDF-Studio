#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/edit/EditSession.hpp"
#include "engine/edit/TextLayout.hpp"

#include "fpdfview.h"

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QLineF>
#include <QList>
#include <QPainterPath>
#include <QSet>
#include <QString>
#include <QTransform>

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
    struct ScanInk { QLineF along; double size { 0.0 }; QLineF word; int side { 0 }; };
    QList<ScanInk> keptOnScan(bool words = false) const;
    struct ScanMove {
        QLineF word; double size { 0.0 }; QPointF by; bool starts { false }; bool ends { false }; bool cutLeft { false }; bool cutRight { false };
        int figure { -1 }; int kind { 0 }; bool borrowed { false }; QRectF onto;
        QColor tint; double opacity { 1.0 }; double scale { 1.0 }; QPointF from;
    };
    QList<ScanMove> movedOnScan() const;
    // The characters a typed one may be taken from the scan for, of one kind, with their outlines in the edit's face.
    struct Glyphs { QString chars; QList<QPainterPath> paths; };
    QList<Glyphs> shapes() const;
    struct ScanCut { QPointF at; double size { 0.0 }; int letters { 0 }; int before { 0 }; double back { 0.0 }; int side { 0 }; double left { 0.0 }; double right { 0.0 }; };
    QList<ScanCut> cutsOnScan() const;
    void setCuts(const QList<ScanCut> &cuts, const QList<double> &shiftsPt, const QList<bool> &outward);
    QList<bool> changedOnScan() const;
    void setAgain(int move);
    struct Outline { QPainterPath path; double strokePt { 0.0 }; QColor fill; bool chosen { false }; double opacity { 1.0 }; };
    bool printedOnScan() const;
    void setLayered(bool layered) { m_layered = layered; }
    void liftFromScan();
    double scanScale() const { return m_scanScale; }
    QList<Outline> printed() const { return m_printed; }
    QColor paper() const { return m_edit.bgColor; }
    QList<Outline> sample() const;
    QString sampleKey() const;
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
        bool     hidden { false };
    };
    struct Moved {
        int     from { 0 };
        int     to { 0 };
        int     source { 0 };
        int     at { 0 };
        QPointF by;
        bool    pixels { true };
        bool    cutLeft { false };
        bool    cutRight { false };
        double  shiftLeft { 0.0 };
        double  shiftRight { 0.0 };
        bool    copied { false };
        bool    borrowed { false };
        QColor  tint;
    };
    // Some letters of a scanned line as the edit has it that follow each other as the scan has them.
    struct ScanWord {
        int     from { 0 };
        int     to { 0 };
        int     line { -1 };
        int     at { 0 };
        QPointF by;
        QColor  tint;
    };
    // The cuts inside words where what a line keeps at its start and its end ends.
    struct Part {
        bool   headCut { false };
        bool   tailCut { false };
        double headShift { 0.0 };
        double tailShift { 0.0 };
        int    headWords { 0 };
        int    tailWords { 0 };
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
    FPDF_FONT fontOf(int style, char32_t codePoint) const;
    int styleAt(int position) const;
    TextLayout::Advance advanceFunction() const;
    void keepScanInk();
    QPointF scanShift() const;
    QTransform turnOnScan() const;
    QColor tintAt(int position, int source) const;
    bool chosenColour(int style) const;
    void copyFigures(int n);
    static void joinTouching(QList<Moved> &moved, QList<std::pair<bool, bool>> &outward);
    QList<ScanWord> scanRuns(int n) const;
    void insertRuns(FPDF_PAGE page, const QList<Placed> &glyphs, double baseline,
                    const PdfiumPageSpace &space) const;
    void underline(FPDF_PAGE page, int n, double lineX, double baseline, double inkStart,
                   const PdfiumPageSpace &space) const;
    void insertRun(FPDF_PAGE page, int style, FPDF_FONT font, const QList<uint> &codePoints,
                   const QList<double> &x, double baseline, const PdfiumPageSpace &space,
                   bool hidden = false) const;
    bool insertOnScan(FPDF_PAGE page, int style, FPDF_FONT font, const QList<uint> &codes,
                      const QList<double> &x, double baseline, const PdfiumPageSpace &space, bool hidden) const;
    bool outlineOf(int style, FPDF_FONT font, const QList<uint> &codePoints, const QList<double> &x,
                   double baseline, Outline *outline, bool onScan = false) const;

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
    bool              m_marked { false };
    TextLayout::Result m_layout;
    QList<std::pair<int, int>> m_scanKept;
    QList<QList<Moved>> m_scanMoved;
    QList<Part> m_scanParts;
    mutable QList<Outline> m_printed;
    bool              m_layered { true };
    bool              m_lifted { false };
    double            m_scanScale { 1.0 };
};

}

#endif
