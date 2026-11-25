#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumText.hpp"

#include "fpdfview.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>

class PdfiumPageSpace;

/// Changes a scan in its own pixels: moves its words, clears their ink and prints new text as it printed its own.
namespace PdfiumScanInk {

struct Piece {
    QImage pixels;
    QRectF at;
};

/// How a scan printed its text, found on its own words.
struct Look {
    double gainPt   { 0.0 };
    double blurPx   { 0.7 };
    double contrast { -1.0 };
    bool   bilevel  { false };
    double cut      { 0.5 };
};

/// What an edit of a scan shows in the scan's own pixels: the words it only moves and the look of what it sets.
struct Edited {
    QList<Piece> moved;
    Look         look;
};

/// Pixels read from the pages of one open document, which never change while it is open.
class Cache
{
public:
    QImage find(int page, const QRect &px, int pageWidthPx, bool cleared = false) const;
    void add(int page, const QRect &px, int pageWidthPx, const QImage &pixels, bool cleared = false);
    bool findLook(const QString &key, Look *look) const;
    void addLook(const QString &key, const Look &look);
    bool findLetters(int page, int pageWidthPx, QList<QRect> *letters) const;
    void addLetters(int page, int pageWidthPx, const QList<QRect> &letters);
    QImage findWhole(int page, const QSize &pagePx) const;
    void addWhole(int page, const QSize &pagePx, const QImage &pixels);
    void clear();

private:
    struct Entry {
        int    page;
        QRect  px;
        int    pageWidthPx;
        QImage pixels;
        bool   cleared;
    };
    struct Letters {
        int          page;
        int          pageWidthPx;
        QList<QRect> boxes;
    };
    struct Whole {
        int    page;
        QSize  pagePx;
        QImage pixels;
    };
    QList<Entry> m_entries;
    QList<std::pair<QString, Look>> m_looks;
    QList<Letters> m_letters;
    QList<Whole> m_wholes;
};

QList<QRectF> pictures(FPDF_PAGE page);

QList<QRectF> withoutKept(const QList<QRectF> &areas, const QList<PdfiumText::Typeset::ScanInk> &kept);
bool inKept(const QPointF &at, const QList<PdfiumText::Typeset::ScanInk> &kept);
void paintOver(FPDF_PAGE page, const QList<QRectF> &areas, const QColor &paper, const PdfiumPageSpace &space);

QList<Piece> moved(FPDF_PAGE page, int pageIndex, const QList<QRectF> &areas,
                   const QList<PdfiumText::Typeset::ScanMove> &moves, const QList<PdfiumText::Typeset::Glyphs> &shapes,
                   QList<int> *missed, Cache *cache);

QList<Piece> cleared(FPDF_PAGE page, int pageIndex, const QList<QRectF> &areas, QList<QRectF> *missed,
                     Cache *cache);

Look lookOf(FPDF_PAGE page, int pageIndex, const QList<PdfiumText::Typeset::Outline> &sample,
            const QRectF &around, Cache *cache);

bool layered(FPDF_PAGE page);
Edited edited(FPDF_PAGE page, int pageIndex, const EditSession::Edit &edit, PdfiumText::Typeset &plan,
              bool layered, Cache *cache);

void insert(FPDF_DOCUMENT doc, FPDF_PAGE page, const QList<Piece> &pieces, const PdfiumPageSpace &space);

void print(FPDF_DOCUMENT doc, FPDF_PAGE page, const QList<PdfiumText::Typeset::Outline> &text,
           const Look &look, const QColor &paper, const PdfiumPageSpace &space);

double baselineAt(const QLineF &along, double x);
bool onLine(const QRectF &area, const QLineF &along, double size);
const QRectF *ownArea(const QList<QRectF> &areas, const QLineF &along, const QLineF &word, double size, bool part = false);

}

#endif
