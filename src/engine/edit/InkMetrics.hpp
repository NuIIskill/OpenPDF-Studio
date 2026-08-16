#pragma once

// Pixel-level measurements on rendered page images.
//
// Everything an edit needs to know about text it did not draw itself has to be
// read back off the render: what color the glyphs are, what sits behind them,
// and how tall the ink actually stands. None of that can be asked of the PDF —
// the Poppler backend reports no font family at all, backgrounds hide in form
// XObjects and shadings, and only the qpdf scanner ever sees a real /Tf size.
//
// These are pure functions over a QImage. They hold no state and touch no
// widget, which is what makes the edit pipeline's hardest guesses testable in
// isolation.

#include <QColor>
#include <QFont>
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QString>

namespace InkMetrics {

/// The ink of one text line inside `boundsPt`, in PDF points, measured on a
/// render of the page at `scale` px/pt. height 0 = nothing measurable found.
struct MeasuredInk { double top { 0.0 }; double height { 0.0 }; double left { 0.0 }; };

/// What a font puts on the page, per 1 pt of font size, when the text is drawn
/// with its pen at the origin.
struct FontInk {
    double heightPerPt { 0.0 };   // ink height
    double risePerPt   { 0.0 };   // baseline → ink top (positive = above)
    double bearingPerPt{ 0.0 };   // pen x → ink left
};

/// Text color = the color of the GLYPH CORES, i.e. the darkest percentile of
/// the region. A most-frequent-non-white heuristic breaks on backends whose
/// antialiasing dominates (thin fonts at low dpi render mostly light grey and
/// the mode lands on a wash-out — the committed text then paints ghostly).
/// Falls back to near-black when the region holds nothing dark.
QColor sampleTextColor(const QImage &img, const QRect &region);

/// Dominant color INSIDE the text bounds — that is the background the blank
/// fill must reproduce. Inside a text rect the background always outweighs the
/// glyph pixels by area, so the mode is the background color; this holds for
/// white pages, colored table rows, and dark headers with light text alike.
/// (A ring AROUND the bounds is unusable: for table cells it runs exactly
/// along the dark border lines and returns the border color.)
/// Works on every backend, including backgrounds the content-stream scan
/// can't see (form XObjects, shadings, images).
/// Returns an invalid color when the region is empty.
QColor sampleBackgroundColor(const QImage &img, const QRect &region);

/// Measures the ink of the text inside `boundsPt` on a page rendered at
/// `scale` px/pt. See MeasuredInk.
MeasuredInk measuredInkPt(const QImage &img, const QRectF &boundsPt, qreal scale);

/// What `f` puts on the page per 1 pt of font size, for `text`.
///
/// It DRAWS the text and measures the result instead of asking the font for its
/// metrics: a tight bounding rect is only as truthful as the platform's font
/// backend, and where that backend has no font at all it reports a full em box
/// while painting something much smaller. Rendering compares what the user will
/// actually see against what the page actually shows.
///
/// The last two numbers turn a measured ink position back into a pen position,
/// which is how backends that cannot read the text matrix (Poppler) still get
/// an anchor to put replacement text on.
FontInk fontInkPerPt(const QString &text, QFont f);

}   // namespace InkMetrics
