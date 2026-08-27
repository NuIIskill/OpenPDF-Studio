#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QRectF>
#include <QString>

#include "ui/view/PageCanvas.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentSource.hpp"
#  include "engine/render/PdfRenderer.hpp"
#endif

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

//
// Anchors come in as canvas coords; the selection itself is stored in PDF
// points so it survives zoom changes and relayouts.
//
// Without a PDF backend every method is a no-op, so callers never need to
// guard the calls.
class TextSelectionController : public QObject
{
    Q_OBJECT

public:
    struct SelectionPart {
        int           page { -1 };
        QList<QRectF> rects;
    };

    explicit TextSelectionController(PageCanvas *canvas, QObject *parent = nullptr);

#ifdef HAVE_PDF_RENDERING
    /// Beides bleibt über Dateiwechsel hinweg gültig: der Renderer zeigt aufs
    /// Backend, und das Dokument wird bei jeder Abfrage frisch aus der Quelle
    /// geholt. Vorher lag hier ein roher Dokumentzeiger, der nach jedem
    /// Wiederöffnen neu gesetzt werden musste — wurde er vergessen, las die
    /// Auswahl freigegebenen Speicher.
    void setSource(PdfRenderer *renderer, const DocumentSource *source);
#endif

    // Mouse handling. The press never consumes the event (the view still wants
    // it for focus and scrolling); move and release report true once a drag is
    // actually running.
    void handlePress(const QPoint &canvasPos);
    bool handleMove(const QPoint &canvasPos);
    bool handleRelease();

    // After zoom or a relayout: reposition the highlights, keep the selection.
    void relayout();
    void clear();

    bool    hasSelection() const { return !m_parts.isEmpty(); }
    QString selectedText() const;
    QList<SelectionPart> selectedParts() const;
    void    copyToClipboard() const;

Q_SIGNALS:
    // Raised so the view can take focus for Ctrl+C when a drag starts.
    void focusRequested();

private:
    struct Part {
        int           page;
        QList<QRectF> rects;   // PDF points, top-left origin
        QString       text;
    };

    // Maps a canvas position onto a page anchor, clamping to the nearest page
    // when the cursor is in a margin / between pages.
    bool anchorAt(const QPoint &canvasPos, int *page, QPointF *pdfPt) const;
    void updateSelection(const QPoint &canvasFrom, const QPoint &canvasTo);
    void updateOverlays();
    PageCanvas *m_canvas { nullptr };

    QList<Part>       m_parts;
    QList<QWidget *>  m_overlays;

    bool   m_tracking { false };
    bool   m_dragging { false };
    QPoint m_dragStart;   // canvas coords

#ifdef HAVE_PDF_RENDERING
    PdfRenderer          *m_renderer { nullptr };
    const DocumentSource *m_src      { nullptr };
#endif
};
