#pragma once

#include "engine/edit/ContentMap.hpp"
#include "engine/edit/TextBlock.hpp"
#include "engine/edit/TextBoxProperties.hpp"
#include <QColor>
#include <QList>
#include <QSet>
#include <QImage>
#include <QString>


// Stores pending text and image edits for one document session.
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
        // Size written to the FILE — the size the PDF itself states whenever
        // that is known, so a save can put it back unchanged.
        double  fontSizePt { 0.0 };
        // 0 → use fontSizePt.
        double  renderSizePt { 0.0 };
        // Where the replaced text started, as an offset from pdfBounds'
        // top-left: x = pen position, y = BASELINE. Kept relative so dragging
        // or resizing the frame carries it along. Only meaningful when
        // hasTextOrigin is set; without it both paint paths fall back to
        // guessing the baseline from the box, and land a few points off.
        QPointF textOriginOffset;
        bool    hasTextOrigin { false };
        // Baseline-to-baseline distance of the replaced block. 0 → use the
        // font's own spacing, which is tighter than most documents' and walks
        // every line after the first up the page.
        double  lineSpacingPt { 0.0 };
        // The text this edit replaces. Every glyph in it is one the original
        // font is proven to carry, which is what lets the vector save keep
        // that font instead of substituting a standard-14 face.
        QString originalText;
        QColor  textColor;
        QColor  bgColor;      // background color used for blank fill
        QString fontFamily;   // Qt family for live paint ("" = default Helvetica)
        bool    bold        { false };
        bool    italic      { false };
        // true → user changed font family/style; vector save switches to a
        // standard-14 font. false → keep the PDF's original font resource.
        bool    fontChanged { false };
        // true → user set the size by hand, so it outranks the /Tf size found
        // in the stream even when the original font is kept.
        bool    sizeChanged { false };
        // Non-empty → this edit sets the value of an AcroForm text field;
        // vector save updates /V + appearances instead of the content stream.
        QString formField;
        // Blank edits: the tight per-line glyph rects to erase. Erasing only
        // the glyphs (not the whole pdfBounds rect) keeps graphics that share
        // the area — chart bars, images, rules — intact. Empty → fall back
        // to erasing pdfBounds.
        QList<QRectF> eraseRects;
        TextBoxProperties box;

        bool operator==(const Edit &o) const {
            return page == o.page && pdfBounds == o.pdfBounds &&
                   sourceRect == o.sourceRect && newText == o.newText &&
                   qFuzzyCompare(fontSizePt + 1.0, o.fontSizePt + 1.0) &&
                   qFuzzyCompare(renderSizePt + 1.0, o.renderSizePt + 1.0) &&
                   textOriginOffset == o.textOriginOffset &&
                   hasTextOrigin == o.hasTextOrigin &&
                   qFuzzyCompare(lineSpacingPt + 1.0, o.lineSpacingPt + 1.0) &&
                   textColor == o.textColor && bgColor == o.bgColor &&
                   fontFamily == o.fontFamily && bold == o.bold &&
                   italic == o.italic && fontChanged == o.fontChanged &&
                   sizeChanged == o.sizeChanged &&
                   formField == o.formField && eraseRects == o.eraseRects &&
                   box == o.box;
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
    // Counter bumped by every image mutation. Text edits are tracked through
    // the undo stack, which knows exactly when the document is back at its
    // saved state; images are placed straight into the session with no undo
    // command behind them, so this is what tells a caller they happened.
    // Monotonic, never reset — equal values mean nothing changed in between.
    quint64 imageRevision() const { return m_imageRevision; }
    void addImageEdit(int page, const QRectF &pdfBounds, const QImage &image);
    void removeImageEdit(int page, const QRectF &pdfBounds);
    bool hasImageEditsOnPage(int page) const;
    void clearImageEdits();
    const QList<ImageEdit> &imageEdits() const { return m_imageEdits; }

    void clear();

    const QList<Edit> &edits() const { return m_edits; }

    // Snapshot / restore for undo-redo.
    QList<Edit> snapshotEdits() const   { return m_edits; }
    void        restoreEdits(QList<Edit> s) { m_edits = std::move(s); }

    bool    hasEditsOnPage(int page) const;
    bool    hasAnyEdits() const { return !m_edits.isEmpty() || !m_imageEdits.isEmpty(); }

    // Returns current edited text at (page, pdfBounds), or null QString if none.
    QString editTextAt(int page, const QRectF &pdfBounds) const;
    // Returns true if pdfPt lies inside a blank (erase-only) session edit that
    // has no companion text at the same bounds — i.e. the intentionally empty
    // source area of a moved/deleted block. Point containment (not exact rect
    // equality) so re-detected native bounds can't sneak past the guard.
    bool isBlankAt(int page, const QPointF &pdfPt) const;
    // Returns true if a blank (without companion) covers ≥ half of `bounds`
    // OR is itself mostly inside `bounds` — i.e. the block found there is
    // (or contains) erased text. Catches clicks that land NEAR the blanked
    // area and fuzzy-snap or merge onto the invisible original.
    bool isBlankCovering(int page, const QRectF &bounds) const;
    // The intentionally emptied areas (companion-less blanks) of a page —
    // exclusion zones for text lookup.
    QList<QRectF> blankRegions(int page) const;
    // Returns stored text color for the edit intersecting pdfBounds, or invalid QColor.
    QColor  editColorAt(int page, const QRectF &pdfBounds) const;

    // Finds the topmost session edit (any kind, including overlay) whose bounds
    // contain pdfPt. Fills *out with the full edit when found.
    bool findEditAt(int page, const QPointF &pdfPt, Edit *out = nullptr) const;

    enum class Paint { Everything, FormFields };

    // Paint replacements onto an already-rendered QImage (used for live view).
    // scale = PDF-point-to-pixel factor used when rendering.
    void applyToImage(int page, QImage &img, qreal scale,
                      Paint what = Paint::Everything) const;

private:
    static void paintTextEdit(QPainter &p, const Edit &e, qreal scale);

    static void paintBackgroundPatch(QPainter &p, const QImage &img,
                                     const QRect &rectPx);
    static void paintBackgroundPatch(QPainter &p, const QImage &img,
                                     const QList<QRect> &rectsPx);

    // Erases the edit area by reconstructing the surrounding background
    // (paintBackgroundPatch); Edit::bgColor is only the last-resort fallback.
    static void paintBlankEdit(QPainter &p, const QImage &img, const Edit &e,
                               qreal scale);



    QList<Edit>       m_edits;
    QList<Edit>       m_suspendedEdits;  // saved by suspendEditsAt(), restored by restoreSuspended()
    QList<ImageEdit>  m_imageEdits;
    quint64           m_imageRevision { 0 };
};
