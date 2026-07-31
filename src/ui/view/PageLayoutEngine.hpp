#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QRectF>

QT_BEGIN_NAMESPACE
class QFrame;
class QLabel;
class QVBoxLayout;
class QWidget;
QT_END_NAMESPACE

#ifdef HAVE_PDF_RENDERING
class EditSession;
class PdfRenderer;
#endif

// Builds and maintains the page widgets — single column and grid.
//
// Knows the renderer and the session, but no mouse interaction: everything
// that needs the QScrollArea itself (swapping the viewport widget, scrolling,
// the viewport width) stays in DocumentView and is passed in.
class PageLayoutEngine : public QObject
{
    Q_OBJECT

public:
    PageLayoutEngine(QWidget *canvas, QVBoxLayout *layout, QWidget *gridCanvas,
                     QObject *parent = nullptr);

#ifdef HAVE_PDF_RENDERING
    void setSource(PdfRenderer *renderer, EditSession *session);
#endif
    void setPageCount(int count) { m_pageCount = count; }
    void setZoom(int percent) { m_zoom = percent; }

    // ── Single-column pages ───────────────────────────────────────────────────
    void buildPages();
    void clearPages();
    void rerenderAll();
    void rerenderPage(int page);
    // Re-renders the page normally then paints a white blank over pdfBoundsPts.
    // Called when starting an edit so the original text disappears from view —
    // the editor widget sits over a clean white area instead of over the old
    // text. eraseRects are the tight glyph rects; empty falls back to bounds.
    void rerenderPageWithBlank(int page, const QRectF &pdfBoundsPts,
                               const QList<QRectF> &eraseRects);

    QLabel *pageLabel(int page) const { return m_pageLabels.value(page, nullptr); }
    int     pageLabelCount()    const { return static_cast<int>(m_pageLabels.size()); }

    // ── Grid ──────────────────────────────────────────────────────────────────
    void buildGridItems();
    // availableWidth is the scroll area's viewport width — only the caller can
    // measure it reliably.
    void relayoutGrid(int availableWidth);
    void clearGrid();
    bool gridActive() const { return m_gridActive; }

Q_SIGNALS:
    // Page geometry changed — overlays anchored to pages have to follow.
    void layoutChanged();
    // A grid card was clicked.
    void pageActivated(int page);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    QWidget     *m_canvas     { nullptr };
    QVBoxLayout *m_layout     { nullptr };
    QWidget     *m_gridCanvas { nullptr };

    QList<QLabel *> m_pageLabels;

    struct GridItem { QFrame *card; QLabel *thumb; QLabel *label; QPixmap original; };
    QList<GridItem>       m_gridItems;
    QHash<QObject *, int> m_gridCardIndex;
    bool                  m_gridActive { false };

    int m_pageCount { 0 };
    int m_zoom      { 100 };

#ifdef HAVE_PDF_RENDERING
    PdfRenderer *m_renderer { nullptr };
    EditSession *m_session  { nullptr };
#endif
};
