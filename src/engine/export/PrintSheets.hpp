#pragma once

#include <QImage>
#include <QList>
#include <QRectF>
#include <QSize>

#ifdef HAVE_PDF_RENDERING

#include "engine/export/DocumentExporter.hpp"

QT_BEGIN_NAMESPACE
class QPainter;
#  ifdef HAVE_QT_PRINT
class QPrinter;
#  endif
QT_END_NAMESPACE

/// The choices of a print job that are not printer settings.
struct PrintJob {
    enum class Scaling { Fit, ShrinkOversize, ActualSize };

    QList<int> pages;
    int        pagesPerSheet { 1 };
    int        posterSheets  { 1 };
    Scaling    scaling       { Scaling::Fit };
    bool       grayscale     { false };
    bool       cutMarks      { false };
    qreal      maxRenderDpi  { 300.0 };
};

/// Lays the pages of a print job out on sheets and paints them, for the preview and the printer alike.
class PrintSheets
{
public:
    PrintSheets(const DocumentExporter::Sources &src, const PrintJob &job);

    static QSize grid(int pagesPerSheet, bool landscapeSheet);
    static QSize posterGrid(int sheets, bool landscapeSheet);

    int        sheetCount() const;
    QList<int> pagesOnSheet(int sheet) const;
    QSizeF     pageSizePts(int page) const;

    void paint(QPainter &painter, int sheet, const QRectF &printable, qreal dpi,
               qreal maxRenderDpi) const;
    QImage preview(int sheet, const QSizeF &paperPts, const QRectF &printablePts,
                   qreal pixelsPerPt) const;
#ifdef HAVE_QT_PRINT
    bool print(QPrinter *printer) const;
#endif

private:
    struct RenderedPage {
        int    page  { -1 };
        qreal  scale { 0.0 };
        QImage image;
    };

    void drawPage(QPainter &painter, int page, const QRectF &area, const QRectF &clip,
                  qreal dpi, qreal maxRenderDpi, PrintJob::Scaling scaling) const;
    void paintPosterTile(QPainter &painter, int sheet, const QRectF &printable, qreal dpi,
                         qreal maxRenderDpi) const;

    DocumentExporter::Sources m_src;
    PrintJob                  m_job;
    mutable RenderedPage      m_cache;
};

#endif
