#pragma once

#include "ContentMap.hpp"
#include "TextBlock.hpp"
#include <QColor>
#include <QList>
#include <QImage>
#include <QString>

#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#endif

// Stores all pending text and image edits for one document session.
// Knows how to apply them to a QImage (live view) and to save a final PDF.
// When qpdf is available (HAVE_QPDF), saveToFile uses a hybrid approach:
//   unedited pages are copied as-is (full vector quality); edited pages get
//   their content stream rewritten (original text ops removed, replacement
//   text appended). AcroForm field edits update /V + appearance streams.
// Without qpdf, falls back to raster rendering (QPdfWriter + QPainter).
class EditSession
{
public:
    // Image overlay embedded in the document (inserted via image tool).
    struct ImageEdit {
        int    page;
        QRectF pdfBounds;  // position/size in PDF points (top-left origin)
        QImage image;
    };

    // A single text edit entry (public so undo/redo commands can snapshot them).
    struct Edit {
        int     page       { -1 };
        QRectF  pdfBounds;
        QRectF  sourceRect;
        QString newText;
        double  fontSizePt { 0.0 };
        QColor  textColor;
        QColor  bgColor;      // background color used for blank fill
        QString fontFamily;   // Qt family for live paint ("" = default Helvetica)
        bool    bold        { false };
        bool    italic      { false };
        // true → user changed font family/style; vector save switches to a
        // standard-14 font. false → keep the PDF's original font resource.
        bool    fontChanged { false };
        // Non-empty → this edit sets the value of an AcroForm text field;
        // vector save updates /V + appearances instead of the content stream.
        QString formField;

        bool operator==(const Edit &o) const {
            return page == o.page && pdfBounds == o.pdfBounds &&
                   sourceRect == o.sourceRect && newText == o.newText &&
                   qFuzzyCompare(fontSizePt + 1.0, o.fontSizePt + 1.0) &&
                   textColor == o.textColor && bgColor == o.bgColor &&
                   fontFamily == o.fontFamily && bold == o.bold &&
                   italic == o.italic && fontChanged == o.fontChanged &&
                   formField == o.formField;
        }
        bool operator!=(const Edit &o) const { return !(*this == o); }
    };

    EditSession() = default;

    // Full-struct insert — preferred; carries font and form-field metadata.
    void addEdit(Edit e) { m_edits.append(std::move(e)); }

    // Legacy convenience overload (blank edits, tests).
    // fontSizePt=0 means "auto-detect from bound-height".
    void addEdit(int page, const QRectF &pdfBounds, const QString &newText,
                 double fontSizePt = 0.0, const QColor &color = QColor(),
                 const QRectF &sourceRect = QRectF(),
                 const QColor &bgColor = QColor());
    void removeEdit(int page, const QRectF &pdfBounds);
    // Removes every edit on 'page' that overlaps 'pdfBounds'.
    void removeAllAt(int page, const QRectF &pdfBounds);

    // Suspend/restore — used when opening the editor over an existing edit.
    // suspendEditsAt() removes all edits at the exact bounds and saves them.
    // clearSuspended()   — called on commit/live-update: discard the snapshot.
    // restoreSuspended() — called on cancel: put the removed edits back.
    void suspendEditsAt(int page, const QRectF &pdfBounds);
    void clearSuspended();
    void restoreSuspended();

    // ── Image edits ────────────────────────────────────────────────────────────
    void addImageEdit(int page, const QRectF &pdfBounds, const QImage &image);
    void removeImageEdit(int page, const QRectF &pdfBounds);
    bool hasImageEditsOnPage(int page) const;
    void clearImageEdits();
    const QList<ImageEdit> &imageEdits() const { return m_imageEdits; }

    void clear();

    // Snapshot / restore for undo-redo.
    QList<Edit> snapshotEdits() const   { return m_edits; }
    void        restoreEdits(QList<Edit> s) { m_edits = std::move(s); }

    bool    hasEditsOnPage(int page) const;
    bool    hasAnyEdits() const { return !m_edits.isEmpty() || !m_imageEdits.isEmpty(); }

    // Returns current edited text at (page, pdfBounds), or null QString if none.
    QString editTextAt(int page, const QRectF &pdfBounds) const;
    // Returns true if there is a blank (erase-only) session edit at this location.
    bool isBlankAt(int page, const QRectF &pdfBounds) const;
    // Returns stored text color for the edit intersecting pdfBounds, or invalid QColor.
    QColor  editColorAt(int page, const QRectF &pdfBounds) const;

    // Finds the topmost session edit (any kind, including overlay) whose bounds
    // contain pdfPt. Fills *out with the full edit when found.
    bool findEditAt(int page, const QPointF &pdfPt, Edit *out = nullptr) const;

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
    static void paintTextEdit(QPainter &p, const Edit &e, qreal scale);
    // Fills the edit area with Edit::bgColor (white when invalid).
    static void paintBlankEdit(QPainter &p, const Edit &e, qreal scale);

#ifdef HAVE_QPDF
    // Hybrid: unedited pages copied as vector, edited pages get their content
    // stream rewritten; form-field edits update /V + appearance streams.
    bool saveVector(const QString &sourcePath, const QString &outputPath,
                    QPdfDocument *doc, int pageCount) const;
#endif

#ifdef HAVE_QT_PDF
    bool saveRaster(const QString &outputPath, QPdfDocument *doc, int pageCount) const;
#endif

    QList<Edit>       m_edits;
    QList<Edit>       m_suspendedEdits;  // saved by suspendEditsAt(), restored by restoreSuspended()
    QList<ImageEdit>  m_imageEdits;
};
