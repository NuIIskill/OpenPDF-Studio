#pragma once

#include "TextBlock.hpp"
#include <QColor>
#include <QList>
#include <QImage>
#include <QString>

#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#endif

// Stores all pending text edits for one document session.
// Knows how to apply them to a QImage (live view) and to save a final PDF.
// When qpdf is available (HAVE_QPDF), saveToFile uses a hybrid approach:
//   unedited pages are copied as-is (full vector quality); edited pages are
//   rasterised at 300 DPI so the original text is truly gone, not overlaid.
// Without qpdf, falls back to raster rendering (QPdfWriter + QPainter).
class EditSession
{
public:
    EditSession() = default;

    // fontSizePt=0 means "auto-detect from content stream or bound-height"
    // sourceRect — the original PDF block this edit belongs to (used for grouping
    // blank+text companion pairs that are created during a drag-move commit).
    // Pass QRectF() (default) when there is no companion pair.
    void addEdit(int page, const QRectF &pdfBounds, const QString &newText,
                 double fontSizePt = 0.0, const QColor &color = QColor(),
                 const QRectF &sourceRect = QRectF());
    void removeEdit(int page, const QRectF &pdfBounds);
    // Removes every edit on 'page' that overlaps 'pdfBounds'.
    void removeAllAt(int page, const QRectF &pdfBounds);

    // Suspend/restore — used when opening the editor over an existing edit.
    // suspendEditsAt() removes all edits overlapping the area and saves them.
    // clearSuspended()   — called on commit/live-update: discard the snapshot.
    // restoreSuspended() — called on cancel: put the removed edits back.
    void suspendEditsAt(int page, const QRectF &pdfBounds);
    void clearSuspended();
    void restoreSuspended();

    void clear();

    bool    hasEditsOnPage(int page) const;
    bool    hasAnyEdits()            const { return !m_edits.isEmpty(); }

    // Returns current edited text at (page, pdfBounds), or null QString if none.
    QString editTextAt(int page, const QRectF &pdfBounds) const;
    // Returns true if there is a blank (erase-only) session edit at this location.
    bool isBlankAt(int page, const QRectF &pdfBounds) const;
    // Returns stored text color for the edit intersecting pdfBounds, or invalid QColor.
    QColor  editColorAt(int page, const QRectF &pdfBounds) const;

    // Finds the first session edit whose bounds contain pdfPt.
    // Returns true and writes bounds/text/fontSizePt/color if found; out-params may be null.
    bool findEditAt(int page, const QPointF &pdfPt,
                    QRectF  *outBounds     = nullptr,
                    QString *outText       = nullptr,
                    double  *outFontSizePt = nullptr,
                    QColor  *outColor      = nullptr) const;

    // Paint replacements onto an already-rendered QImage (used for live view).
    // scale = PDF-point-to-pixel factor used when rendering.
    void applyToImage(int page, QImage &img, qreal scale) const;

    // Write the document with all edits to outputPath.
    //   sourcePath  — original PDF file on disk; enables vector output via qpdf.
    //   doc         — Qt PDF document; used for raster fallback only.
    //   pageCount   — total page count from the reader.
#if defined(HAVE_QT_PDF)
    bool saveToFile(const QString &outputPath,
                    QPdfDocument  *doc,
                    int            pageCount,
                    const QString &sourcePath = QString()) const;
#elif defined(HAVE_QPDF)
    bool saveToFile(const QString &outputPath,
                    int            pageCount,
                    const QString &sourcePath) const;
#endif

private:
    struct Edit {
        int     page;
        QRectF  pdfBounds;
        // The original PDF block this edit belongs to.  For a drag-move commit,
        // blank(P1) and text(P2) both carry sourceRect=P1, letting suspendEditsAt
        // suspend the entire group when either member is hit.  Empty = standalone.
        QRectF  sourceRect;
        QString newText;
        double  fontSizePt { 0.0 };  // 0 = derive from content stream / bound height
        QColor  textColor;           // invalid = use default (near-black)
    };

    static void paintTextEdit(QPainter &p, const Edit &e, qreal scale);
    static void paintBlankEdit(QPainter &p, const Edit &e, qreal scale);

#ifdef HAVE_QPDF
    // Hybrid: unedited pages copied as vector, edited pages rasterised at 300 DPI.
    bool saveVector(const QString &sourcePath, const QString &outputPath,
                    QPdfDocument *doc, int pageCount) const;
#endif

#ifdef HAVE_QT_PDF
    bool saveRaster(const QString &outputPath, QPdfDocument *doc, int pageCount) const;
#endif

    QList<Edit> m_edits;
    QList<Edit> m_suspendedEdits;  // saved by suspendEditsAt(), restored by restoreSuspended()
};
