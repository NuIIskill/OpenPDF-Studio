#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QObject>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QString>

#include "engine/edit/EditSession.hpp"
#include "ui/view/PageCanvas.hpp"

#include <functional>

QT_BEGIN_NAMESPACE
class QFrame;
class QRubberBand;
class QWidget;
QT_END_NAMESPACE

class ImageAnnotation;

#ifdef HAVE_PDF_RENDERING
class PdfRenderer;
#endif

/// Placed images and detected image regions as an overlay layer over the canvas.
class ImageAnnotationLayer : public QObject
{
    Q_OBJECT

public:
    explicit ImageAnnotationLayer(PageCanvas *canvas, QObject *parent = nullptr);

#ifdef HAVE_PDF_RENDERING
    void setSource(PdfRenderer *renderer, EditSession *session);
#endif

    void setToolActive(bool active);
    bool toolActive() const { return m_toolActive; }

    void place(const QImage &img, const QPoint &canvasPos);
    void placeInRect(const QImage &img, const QRect &canvasRect);

    bool takeDetectedRegionAt(const QPoint &canvasPos);
    bool hoverAt(const QPoint &canvasPos);

    void setRubberBand(QRubberBand *band) { m_band = band; }
    bool handlePress(const QPoint &canvasPos);
    bool handleMove(const QPoint &canvasPos, const QPoint &scroll);
    bool handleRelease(const QPoint &scroll);

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

    static QString changesKey() { return QStringLiteral("image-changes"); }
    QByteArray changeState() const;
    void       restoreChanges(const QByteArray &data);

Q_SIGNALS:

    void pageNeedsRerender(int page);

    void imageAdded(int page);
    void imageRemoved(int page);
    void imageChanged(int page, const QString &what);

private:
    void connectAnnotation(ImageAnnotation *ann);

    void addEntry(int page, const QRectF &pdfBounds, const QImage &img,
                  const QRect &canvasRect = QRect(), const QList<int> &key = {});

    struct Entry {
        int        page;
        QRectF     pdfBounds;
        QImage     image;
        QWidget   *widget { nullptr };
        QList<int> key;
        QRectF     from;
    };

    struct Detected {
        int        page;
        QRectF     pdfRect;
        QList<int> key;
    };

    QRect  canvasRect(int page, const QRectF &pdfRect) const;
    void   scanPage(int page);
    int    detectedAt(const QPoint &canvasPos);
    QImage detectedImage(int index) const;
    ImageAnnotation *liftDetected(int index);
    void   removeDetected(int index);

    Entry *entryFor(const ImageAnnotation *ann);
    void   copyImage(const ImageAnnotation *ann);
    void   pasteImage(const ImageAnnotation *ann, const QPoint &canvasPos);
    void   rotateImage(ImageAnnotation *ann, int degrees);
    void   replaceImage(ImageAnnotation *ann);
    void   changeEntry(Entry &e, const QRectF &pdfBounds, const QImage &img,
                       const std::function<void(EditSession::ImageChange &)> &original);
    void   changeOriginal(const Entry &e,
                          const std::function<void(EditSession::ImageChange &)> &edit);

    PageCanvas *m_canvas { nullptr };

    QList<Entry>    m_placed;
    QList<Detected> m_detected;
    QSet<int>       m_scanned;
    QFrame         *m_hoverFrame { nullptr };
    int             m_hovered    { -1 };
    bool            m_toolActive { false };

    QRubberBand *m_band { nullptr };
    bool   m_tracking { false };
    bool   m_dragging { false };
    QPoint m_dragStart;
    QRect  m_dragPageRect;

#ifdef HAVE_PDF_RENDERING
    PdfRenderer *m_renderer { nullptr };
    EditSession *m_session  { nullptr };
#endif
};
