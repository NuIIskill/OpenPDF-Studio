#pragma once

#include <QObject>
#include <QPoint>
#include <QString>

QT_BEGIN_NAMESPACE
class QScrollArea;
class QVBoxLayout;
class QWheelEvent;
QT_END_NAMESPACE

class PageCanvas;
class PageLayoutEngine;

/// Owns the zoom level and everything that makes zooming feel stable: the
/// wheel policy (what counts as a zoom gesture) and the scroll anchoring that
/// holds the point under the cursor still while the pages resize.
///
/// It drives the scroll area and the layout engine directly, but knows nothing
/// about what else is anchored to a page. Overlays follow through zoomApplied().
class ZoomController : public QObject
{
    Q_OBJECT

public:
    ZoomController(QScrollArea *area, PageCanvas *canvas, QVBoxLayout *canvasLayout,
                   PageLayoutEngine *engine, QObject *parent = nullptr);

    int  zoom() const { return m_zoom; }

    /// Zoom from the toolbar, the menu or a shortcut: holds the middle of the
    /// viewport. Without an anchor the canvas keeps its top-left corner and the
    /// passage the user was reading slides out of view.
    void setZoom(int percent);

    /// Zoom, keeping the document point under `viewportAnchor` in place.
    void applyZoom(int percent, const QPoint &viewportAnchor);

    void setSettings(int step, bool ctrlWheel, bool toPointer,
                     const QString &wheelAction);

    /// Handles a wheel event as a zoom gesture. Returns false when the event is
    /// not one and the caller has to scroll instead.
    bool handleWheel(QWheelEvent *e);

    /// Makes the scroll area take new page sizes into account NOW — it would
    /// otherwise resize its canvas (and with it the scrollbar ranges) only once
    /// the posted layout request is handled, which is after the anchoring below
    /// has already read them.
    void updateScrollRange();

Q_SIGNALS:
    /// New zoom level, emitted as soon as it reached the layout engine.
    void zoomChanged(int percent);
    /// The scroll position moved — whoever tracks the visible window has to
    /// resync it.
    void viewportChanged();
    /// Zoom and scroll position have settled; overlays anchored to pages
    /// (selection highlights, the editor frame) must reposition now.
    void zoomApplied(int percent);

private:
    QScrollArea      *m_area   { nullptr };
    PageCanvas       *m_canvas { nullptr };
    QVBoxLayout      *m_layout { nullptr };
    PageLayoutEngine *m_engine { nullptr };

    int     m_zoom              { 100 };
    int     m_zoomStep          { 10 };
    bool    m_ctrlWheelEnabled  { true };
    bool    m_zoomToPointer     { true };
    QString m_wheelAction       { QStringLiteral("scroll") };
};
