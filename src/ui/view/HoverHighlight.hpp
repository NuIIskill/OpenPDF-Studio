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

/// Edit-mode hover feedback: outlines the detected content region under the cursor and names it in a tooltip.
class HoverHighlight : public QObject
{
    Q_OBJECT

public:
    HoverHighlight(PageCanvas *canvas, QObject *parent = nullptr);

#ifdef HAVE_PDF_RENDERING

    void setSource(ContentProvider *provider, EditSession *session);

    void setEditorFrame(const QWidget *frame) { m_editorFrame = frame; }
#endif

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
