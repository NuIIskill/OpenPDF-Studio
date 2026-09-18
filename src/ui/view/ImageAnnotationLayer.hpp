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

/// Placed images and detected image regions as an overlay layer over the canvas.
class ImageAnnotationLayer : public QObject
{
    Q_OBJECT

public:
    explicit ImageAnnotationLayer(PageCanvas *canvas, QObject *parent = nullptr);

#ifdef HAVE_PDF_RENDERING
    void setSource(PdfRenderer *renderer, EditSession *session,
                   OcrEngine *ocr, const QString &filePath);
#endif

    void setToolActive(bool active);
    bool toolActive() const { return m_toolActive; }

    void place(const QImage &img, const QPoint &canvasPos);
    void placeInRect(const QImage &img, const QRect &canvasRect);

    bool takeDetectedRegionAt(const QPoint &canvasPos);

    bool handlePress(const QPoint &canvasPos);

    QPoint handleMove(const QPoint &canvasPos, bool *dragging);

    bool handleRelease();
    bool   isDragTracking() const { return m_tracking; }
    QPoint dragStart()      const { return m_dragStart; }

    void showContextMenu(ImageAnnotation *ann, const QPoint &globalPos);

    void relayout();

    void clear();

    void scanVisiblePage(int firstVisiblePage);
    void clearDetectedHighlights();

    /// Stores an image placement for document saving.
    struct Placed { int page; QRectF pdfBounds; QImage image; };
    QList<Placed> placedImages() const;
    bool          isEmpty() const { return m_placed.isEmpty(); }

    void restoreImages(const QList<Placed> &images);

Q_SIGNALS:

    void pageNeedsRerender(int page);

    void imageAdded(int page);
    void imageRemoved(int page);

private:
    void connectAnnotation(ImageAnnotation *ann);

    void addEntry(int page, const QRectF &pdfBounds, const QImage &img,
                  const QRect &canvasRect = QRect());

    struct Entry {
        int     page;
        QRectF  pdfBounds;
        QImage  image;
        QWidget *widget { nullptr };
    };

    PageCanvas *m_canvas { nullptr };

    QList<Entry>    m_placed;
    QList<QFrame *> m_detectedFrames;
    QImage          m_clipboard;
    bool            m_toolActive { false };

    bool   m_tracking { false };
    bool   m_dragging { false };
    QPoint m_dragStart;
    QRect  m_dragPageRect;

#ifdef HAVE_PDF_RENDERING
    PdfRenderer *m_renderer { nullptr };
    EditSession *m_session  { nullptr };
    OcrEngine   *m_ocr      { nullptr };
    QString      m_filePath;
#endif
};
