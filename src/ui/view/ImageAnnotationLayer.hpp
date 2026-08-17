#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QRect>
#include <QRectF>
#include <QString>

#include "ui/view/PageCanvas.hpp"

QT_BEGIN_NAMESPACE
class QFrame;
class QWidget;
QT_END_NAMESPACE

class ImageAnnotation;
class OcrEngine;

#ifdef HAVE_PDF_RENDERING
class EditSession;
class PdfRenderer;
#endif

// Placed images and detected image regions as an overlay layer over the canvas.
//
// Positions are held in PDF points; the widgets are only derived from them, so
// a zoom change is a pure relayout(). The whole API works in CANVAS coords —
// the caller converts from viewport coords where needed.
//
// Without a PDF backend the layer still places images as plain overlays; only
// the session write-back and the region scan are compiled out.
class ImageAnnotationLayer : public QObject
{
    Q_OBJECT

public:
    explicit ImageAnnotationLayer(PageCanvas *canvas, QObject *parent = nullptr);

#ifdef HAVE_PDF_RENDERING
    void setSource(PdfRenderer *renderer, EditSession *session,
                   OcrEngine *ocr, const QString &filePath);
#endif

    // Images are interactive only while the image tool is active.
    void setToolActive(bool active);
    bool toolActive() const { return m_toolActive; }

    void place(const QImage &img, const QPoint &canvasPos);
    void placeInRect(const QImage &img, const QRect &canvasRect);

    // Press on a detected region extracts that region and places it as an
    // image. Returns true when a region was hit and consumed.
    bool takeDetectedRegionAt(const QPoint &canvasPos);

    // Drag-to-frame. handlePress returns false when the press was outside any
    // page, in which case the caller should swallow the event without starting
    // a drag. The finished rect is reported via rectDragged().
    bool handlePress(const QPoint &canvasPos);
    // Clamps to the page the drag started on and returns the clamped position;
    // dragging is true once the drag threshold is passed.
    QPoint handleMove(const QPoint &canvasPos, bool *dragging);
    // Returns true when a drag was running.
    bool handleRelease();
    bool   isDragTracking() const { return m_tracking; }
    QPoint dragStart()      const { return m_dragStart; }

    void showContextMenu(ImageAnnotation *ann, const QPoint &globalPos);

    // After zoom or a relayout: reposition the overlays.
    void relayout();
    // Drop every placed image and its widget (document closed).
    void clear();

    // Highlight the images already present in the PDF on the first visible page.
    void scanVisiblePage(int firstVisiblePage);
    void clearDetectedHighlights();

    // For the save path: everything that has to be drawn. Deliberately without
    // the overlay widget — saving has nothing to do with widgets.
    struct Placed { int page; QRectF pdfBounds; QImage image; };
    QList<Placed> placedImages() const;
    bool          isEmpty() const { return m_placed.isEmpty(); }

    // Replaces every placed image with `images` — widgets and session edits
    // both. This is how the history goes back to an earlier image state; it
    // deliberately reports no imageAdded/imageRemoved, since restoring a state
    // is not a change to record.
    void restoreImages(const QList<Placed> &images);

Q_SIGNALS:
    // The page content changed (image added, moved or removed).
    void pageNeedsRerender(int page);
    // A placement the document history records. Separate from
    // pageNeedsRerender, which also fires for moves and for restores.
    void imageAdded(int page);
    void imageRemoved(int page);

private:
    void connectAnnotation(ImageAnnotation *ann);
    // Creates the overlay widget for an entry and appends it to m_placed.
    // The single place that knows how a placed image becomes a widget.
    void addEntry(int page, const QRectF &pdfBounds, const QImage &img,
                  const QRect &canvasRect = QRect());

    struct Entry {
        int     page;
        QRectF  pdfBounds;   // PDF points
        QImage  image;
        QWidget *widget { nullptr };   // ImageAnnotation overlay
    };

    PageCanvas *m_canvas { nullptr };

    QList<Entry>    m_placed;
    QList<QFrame *> m_detectedFrames;   // transient highlights for existing images
    QImage          m_clipboard;        // internal copy/cut clipboard
    bool            m_toolActive { false };

    // Drag-to-frame state (canvas coords)
    bool   m_tracking { false };
    bool   m_dragging { false };
    QPoint m_dragStart;
    QRect  m_dragPageRect;   // page bounds for clamping the rubber band

#ifdef HAVE_PDF_RENDERING
    PdfRenderer *m_renderer { nullptr };
    EditSession *m_session  { nullptr };
    OcrEngine   *m_ocr      { nullptr };
    QString      m_filePath;
#endif
};
