#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QRectF>
#include <QString>

#include "engine/edit/TextLayout.hpp"
#include "ui/view/PageCanvas.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentSource.hpp"
#  include "engine/render/PdfRenderer.hpp"
#endif

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

/// Anchors come in as canvas coords; the selection itself is stored in PDF points so it survives zoom changes and relayouts.
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

    void setSource(PdfRenderer *renderer, const DocumentSource *source);
#endif

    void handlePress(const QPoint &canvasPos, bool extend = false);
    bool handleMove(const QPoint &canvasPos);
    bool handleRelease();

    void relayout();
    void clear();

    bool    hasSelection() const { return !m_parts.isEmpty(); }
    QString selectedText() const;
    QList<SelectionPart> selectedParts() const;
    void    copyToClipboard() const;

Q_SIGNALS:

    void focusRequested();

private:
    struct Part {
        int           page;
        QList<QRectF> rects;
        QString       text;
        QList<TextLayout::Source> sources;
        QList<TextLayout::OriginalLine> lines;
    };

    bool anchorAt(const QPoint &canvasPos, int *page, QPointF *pdfPt) const;
    void updateSelection(const QPoint &canvasTo);
    void updateOverlays();
    PageCanvas *m_canvas { nullptr };

    QList<Part>       m_parts;
    QList<QWidget *>  m_overlays;

    bool    m_tracking   { false };
    bool    m_dragging   { false };
    QPoint  m_dragStart;
    int     m_anchorPage { -1 };
    QPointF m_anchorPt;

#ifdef HAVE_PDF_RENDERING
    PdfRenderer          *m_renderer { nullptr };
    const DocumentSource *m_src      { nullptr };
#endif
};
