#pragma once

#include <QObject>
#include <QPoint>
#include <QRectF>

QT_BEGIN_NAMESPACE
class QFrame;
class QWidget;
QT_END_NAMESPACE

class PageCanvas;

#ifdef HAVE_PDF_RENDERING
class ContentProvider;
class EditSession;
#endif

/// Edit-mode hover feedback: outlines the detected content region under the
/// cursor and names it in a tooltip.
///
/// Owns the overlay frame and the "what is currently outlined" state, so the
/// view only has to say where the pointer is. Without a content provider it is
/// inert, which is also what the non-PDF build gets.
class HoverHighlight : public QObject
{
    Q_OBJECT

public:
    HoverHighlight(PageCanvas *canvas, QObject *parent = nullptr);

#ifdef HAVE_PDF_RENDERING
    /// The model the outline is read from, and the session that knows which
    /// areas have been erased. Either may be null; the highlight stays inert.
    void setSource(ContentProvider *provider, EditSession *session);
    /// While this widget is visible, positions inside it are left alone — the
    /// open editor must not fight the highlight for the same area.
    void setEditorFrame(const QWidget *frame) { m_editorFrame = frame; }
#endif

    /// Outlines the region under `canvasPos`, or hides the outline when there
    /// is nothing there to point at.
    void showAt(const QPoint &canvasPos);
    void hide();

private:
    PageCanvas *m_canvas { nullptr };
    QFrame     *m_frame  { nullptr };
    QRectF      m_bounds;
    int         m_page   { -1 };

#ifdef HAVE_PDF_RENDERING
    ContentProvider *m_provider    { nullptr };
    EditSession     *m_session     { nullptr };
    const QWidget   *m_editorFrame { nullptr };
#endif
};
