#include "DocumentView.hpp"
#include "tools/ImageAnnotation.hpp"
#include "view/ImageAnnotationLayer.hpp"
#include "view/PageLayoutEngine.hpp"
#include "view/TextSelectionController.hpp"

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <cstring>
#endif

#ifdef HAVE_POPPLER
#  include <QPdfWriter>
#  include <QPageSize>
#  include <cmath>
#  include <algorithm>
#endif

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QRubberBand>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QCursor>
#include <QScrollBar>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPalette>
#include <QPainter>
#include <QApplication>
#include <QClipboard>
#include <QTextEdit>
#include <QStyle>
#include <QTimer>
#include <QDebug>
#include <QMap>
#include <QFileDialog>
#include <QFontMetrics>
#include <QMenu>
#include <QRegularExpression>
#include <QUndoCommand>
#include <QStringList>
#include <QKeyEvent>
#include <algorithm>
#include <limits>

#ifdef HAVE_PDF_RENDERING
// Undo/redo command for text edits: stores before/after EditSession snapshots.
// QUndoStack::push() calls redo() immediately, so we skip the first redo() call
// since the edit is already committed at push-time.
class EditUndoCmd : public QUndoCommand
{
    EditSession              *m_session;
    DocumentView             *m_view;
    int                       m_srcPage, m_dstPage;  // differ when the box was dragged onto another page
    QList<EditSession::Edit>  m_before, m_after;
    bool                      m_firstRedo { true };
public:
    EditUndoCmd(EditSession *s, DocumentView *v, int srcPage, int dstPage,
                QList<EditSession::Edit> before,
                QList<EditSession::Edit> after)
        : QUndoCommand(DocumentView::tr("Text bearbeiten"))
        , m_session(s), m_view(v), m_srcPage(srcPage), m_dstPage(dstPage)
        , m_before(std::move(before)), m_after(std::move(after)) {}

    void undo() override {
        m_session->restoreEdits(m_before);
        rerender();
    }
    void redo() override {
        if (m_firstRedo) { m_firstRedo = false; return; }
        m_session->restoreEdits(m_after);
        rerender();
    }
private:
    void rerender() {
        m_view->rerenderPage(m_srcPage);
        if (m_dstPage != m_srcPage)
            m_view->rerenderPage(m_dstPage);
    }
};

#ifdef HAVE_POPPLER
// Find the text line at pdfPt on the given Poppler page.
// Returns the word cluster's bounding rect and concatenated text,
// or an invalid TextBlock if nothing is close enough to pdfPt.
// Poppler-Qt6 text coordinates use screen space (Y=0 at top-left).
// Session-erased areas are invisible to text lookup: a glyph whose center
// lies in an excluded region is treated as if it were not on the page.
static bool popplerExcluded(const QRectF &glyph, const QList<QRectF> &exclude)
{
    for (const QRectF &e : exclude)
        if (e.contains(glyph.center())) return true;
    return false;
}

static TextBlock popplerTextAt(Poppler::Document *doc, int page, const QPointF &pdfPt,
                               const QList<QRectF> &exclude = {})
{
    // Poppler can throw on malformed files — degrade to "no text found".
    try {
    auto popplerPage = doc->page(page);
    if (!popplerPage) return {};

    const auto textBoxes = popplerPage->textList();
    if (textBoxes.empty()) return {};

    // Find the nearest word to the click point (exact hit preferred).
    double bestDist = 40.0;
    int bestIdx = -1;
    for (int i = 0; i < static_cast<int>(textBoxes.size()); ++i) {
        const QRectF bbox = textBoxes[i]->boundingBox();
        if (popplerExcluded(bbox, exclude)) continue;
        if (bbox.contains(pdfPt)) { bestIdx = i; bestDist = 0.0; break; }
        const double dx = qMax(0.0, qMax(bbox.left() - pdfPt.x(), pdfPt.x() - bbox.right()));
        const double dy = qMax(0.0, qMax(bbox.top()  - pdfPt.y(), pdfPt.y() - bbox.bottom()));
        const double d  = std::hypot(dx, dy);
        if (d < bestDist) { bestDist = d; bestIdx = i; }
    }
    if (bestIdx < 0) return {};

    const QRectF anchor = textBoxes[bestIdx]->boundingBox();
    const double lineY  = anchor.center().y();
    const double lineH  = qMax(anchor.height(), 6.0);

    // Collect all words on the same line (within lineH * 0.6 of anchor centre Y)
    struct Word { QRectF bbox; QString text; bool spaceAfter; };
    std::vector<Word> lineWords;
    for (const auto &tb : textBoxes) {
        const QRectF bbox = tb->boundingBox();
        if (popplerExcluded(bbox, exclude)) continue;
        if (std::abs(bbox.center().y() - lineY) < lineH * 0.6)
            lineWords.push_back({ bbox, tb->text(), tb->hasSpaceAfter() });
    }
    if (lineWords.empty()) return {};

    std::sort(lineWords.begin(), lineWords.end(),
              [](const Word &a, const Word &b) { return a.bbox.left() < b.bbox.left(); });

    QRectF lineRect;
    QString lineText;
    for (int i = 0; i < static_cast<int>(lineWords.size()); ++i) {
        lineRect = (i == 0) ? lineWords[i].bbox : lineRect.united(lineWords[i].bbox);
        if (i > 0 && lineWords[i - 1].spaceAfter)
            lineText += QLatin1Char(' ');
        lineText += lineWords[i].text;
    }

    return lineText.isEmpty() ? TextBlock{} : TextBlock{ page, lineRect, lineText };
    } catch (...) { return {}; }
}

struct PopplerWord { QRectF bbox; QString text; bool spaceAfter; };

// Collects the words of the text block covering `area`, then COMPLETES each
// line: the region model's width estimates can end short of the real line,
// so collected lines grow word-by-word over word-sized gaps until the true
// line ends. Column gutters (gaps far wider than a word space) are never
// crossed. Incomplete collection here caused half-erased originals after a
// move — line-end words missing from the erase rects stayed visible.
static std::vector<PopplerWord> popplerCollectBlockWords(
        Poppler::Document *doc, int page, const QRectF &area,
        const QList<QRectF> &exclude = {})
{
    std::vector<PopplerWord> all;
    try {
        auto popplerPage = doc->page(page);
        if (!popplerPage) return {};
        for (const auto &tb : popplerPage->textList()) {
            const QRectF b = tb->boundingBox();
            if (!b.isEmpty() && !popplerExcluded(b, exclude))
                all.push_back({ b, tb->text(), tb->hasSpaceAfter() });
        }
    } catch (...) { return {}; }

    std::vector<char> used(all.size(), 0);
    const QRectF seedArea = area.adjusted(-2, -2, 2, 2);
    bool any = false;
    for (size_t i = 0; i < all.size(); ++i)
        if (seedArea.contains(all[i].bbox.center())) { used[i] = 1; any = true; }
    if (!any) return {};

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < all.size(); ++i) {
            if (used[i]) continue;
            const QRectF &w = all[i].bbox;
            for (size_t j = 0; j < all.size(); ++j) {
                if (!used[j]) continue;
                const QRectF &u = all[j].bbox;
                const double lineH = std::max({ u.height(), w.height(), 4.0 });
                if (std::abs(w.center().y() - u.center().y()) > lineH * 0.6)
                    continue;
                const double gap = (w.left() >= u.right())
                                       ? w.left() - u.right()
                                       : u.left() - w.right();
                if (gap < lineH * 1.2) { used[i] = 1; changed = true; break; }
            }
        }
    }

    std::vector<PopplerWord> out;
    for (size_t i = 0; i < all.size(); ++i)
        if (used[i]) out.push_back(all[i]);
    std::sort(out.begin(), out.end(),
              [](const PopplerWord &a, const PopplerWord &b) {
        if (std::abs(a.bbox.center().y() - b.bbox.center().y()) > 3.0)
            return a.bbox.center().y() < b.bbox.center().y();
        return a.bbox.left() < b.bbox.left();
    });
    return out;
}

// Full text of the block covering rect (lines completed, see above), joined
// with '\n', plus the glyph-accurate united bounds of the collected words.
static TextBlock popplerBlockInRect(Poppler::Document *doc, int page, const QRectF &rect,
                                    const QList<QRectF> &exclude = {})
{
    const auto words = popplerCollectBlockWords(doc, page, rect, exclude);
    if (words.empty()) return {};

    QString out;
    QRectF  united = words.front().bbox;
    double  lineY  = words.front().bbox.center().y();
    double  lineH  = qMax(words.front().bbox.height(), 6.0);
    for (size_t i = 0; i < words.size(); ++i) {
        const PopplerWord &w = words[i];
        united = united.united(w.bbox);
        if (i > 0) {
            if (std::abs(w.bbox.center().y() - lineY) > lineH * 0.6) {
                out += u'\n';
                lineY = w.bbox.center().y();
                lineH = qMax(w.bbox.height(), 6.0);
            } else if (words[i - 1].spaceAfter) {
                out += u' ';
            }
        }
        out += w.text;
    }
    out = out.trimmed();
    return out.isEmpty() ? TextBlock{} : TextBlock{ page, united, out };
}

// Tight word boxes of the block covering `area` (lines completed) — used to
// erase ONLY the glyphs.
static QList<QRectF> popplerGlyphRects(Poppler::Document *doc, int page,
                                       const QRectF &area,
                                       const QList<QRectF> &exclude = {})
{
    QList<QRectF> out;
    for (const PopplerWord &w : popplerCollectBlockWords(doc, page, area, exclude))
        out.append(w.bbox);
    return out;
}
#endif // HAVE_POPPLER

#endif // HAVE_PDF_RENDERING


DocumentView::DocumentView(QWidget *parent)
    : QScrollArea(parent)
    , m_undoStack(new QUndoStack(this))
{
    setObjectName(QStringLiteral("DocumentView"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setAlignment(Qt::AlignCenter);
    setAcceptDrops(true);

    m_canvas = new QWidget(this);
    m_canvas->setObjectName(QStringLiteral("DocumentCanvas"));

    m_layout = new QVBoxLayout(m_canvas);
    m_layout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_layout->setContentsMargins(40, 40, 40, 40);
    m_layout->setSpacing(20);

    m_dropHint = new QLabel(tr("Drop a PDF here or click a tab to open"), m_canvas);
    m_dropHint->setObjectName(QStringLiteral("DropHint"));
    m_dropHint->setAlignment(Qt::AlignCenter);
    m_dropHint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_layout->addWidget(m_dropHint);

    setWidget(m_canvas);

    m_gridCanvas = new QWidget();   // no parent — we control it via setWidget()
    m_gridCanvas->setObjectName(QStringLiteral("GridCanvas"));

    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this]() { reportCurrentPage(); });

    viewport()->installEventFilter(this);
    m_canvas->installEventFilter(this);     // also catch events on the canvas itself
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());
    retranslateUi();

    m_selection = new TextSelectionController(this, this);
    connect(m_selection, &TextSelectionController::focusRequested,
            this, [this]() { setFocus(Qt::MouseFocusReason); });

    m_imageLayer = new ImageAnnotationLayer(this, this);
    connect(m_imageLayer, &ImageAnnotationLayer::pageNeedsRerender,
            this, &DocumentView::rerenderPage);

    m_layoutEngine = new PageLayoutEngine(m_canvas, m_layout, m_gridCanvas, this);
    // One place to keep every page-anchored overlay in sync with a relayout —
    // previously each relayout site had to remember both of these by hand.
    connect(m_layoutEngine, &PageLayoutEngine::layoutChanged, this, [this]() {
        m_selection->relayout();
        m_imageLayer->relayout();
    });
    connect(m_layoutEngine, &PageLayoutEngine::pageActivated, this, [this](int page) {
        setViewMode(ViewMode::Single);
        QMetaObject::invokeMethod(this, [this, page]() {
            if (const QLabel *lbl = m_layoutEngine->pageLabel(page))
                verticalScrollBar()->setValue(lbl->mapTo(m_canvas, QPoint(0, 0)).y() - 40);
        }, Qt::QueuedConnection);
    });

#ifdef HAVE_PDF_RENDERING
    m_session     = new EditSession();
    m_editorFrame = new TextBoxFrame(m_canvas);
    connect(m_editorFrame, &TextBoxFrame::committed, this, &DocumentView::commitCurrentEdit);
    connect(m_editorFrame, &TextBoxFrame::cancelled,  this, &DocumentView::cancelCurrentEdit);
    // While the editor is open, its widget IS the live view of the text —
    // nothing is painted onto the page underneath (painting the same text
    // there produced visible doubling on drag/zoom). The page only shows the
    // blank that hides the original text; the session edit is created on commit.
    connect(m_editorFrame, &TextBoxFrame::dragEnded, this, [this]() {
        // Keep the ORIGINAL bounds blanked so the underlying text stays hidden
        // after the box was dragged away from it. The blank always lives on
        // the SOURCE page — the box itself may rest on a different page now.
        if (m_activeEditSourcePage >= 0 && m_activeEditNeedsBlank)
            rerenderPageWithBlank(m_activeEditSourcePage, m_activeEditOriginalBounds);
    });
    connect(m_editorFrame, &TextBoxFrame::boundsChanged, this, [this](const QRectF &inner) {
        if (m_activeEditPage < 0) return;
        // The box is freely draggable across pages: the page under its center
        // owns it. Between pages (margins/gaps) keep the last owner.
        auto [pg, lbl] = pageAtCanvasPos(inner.center().toPoint());
        if (pg < 0 || !lbl) {
            pg  = m_activeEditPage;
            lbl = pageLabel(pg);
            if (!lbl) return;
        }
        if (pg != m_activeEditPage) {
            m_activeEditPage = pg;
            // Growth and resize clamps must use the page the box is on now.
            m_editorFrame->setPageRect(lbl->geometry());
        }
        const qreal scale = PdfRenderer::screenScale(m_zoom);
        QRectF newBounds(
            (inner.topLeft() - QPointF(lbl->pos())) / scale,
            inner.size() / scale);
        clampToPdfPage(pg, newBounds);
        m_activeEditBounds = newBounds;
    });
    // Hover feedback: outlines the detected content region under the cursor
    // in edit mode. Transparent for mouse events so clicks pass through.
    m_hoverHighlight = new QFrame(m_canvas);
    m_hoverHighlight->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_hoverHighlight->hide();

#  ifdef HAVE_QT_PDF
    m_document  = new QPdfDocument(this);
    m_renderer  = new PdfRenderer(m_document);
    m_extractor = new PdfTextExtractor(m_document);
    m_selection->setSource(m_renderer, m_document);
    m_layoutEngine->setSource(m_renderer, m_session);
#  endif
#endif
    // HAVE_POPPLER: m_renderer and m_popplerDoc are created per-file in openFile()

    m_ocrEngine = new OcrEngine();
#ifdef HAVE_PDF_RENDERING
    // After m_ocrEngine exists. On the Poppler path m_renderer is still null
    // here — openFile() hands the real one over per document.
    m_imageLayer->setSource(m_renderer, m_session, m_ocrEngine, m_filePath);
#endif
}

DocumentView::~DocumentView()
{
    delete m_ocrEngine;
#ifdef HAVE_PDF_RENDERING
    // The provider references the backend document (raw pointer on Poppler) —
    // destroy it before the document regardless of member declaration order.
    m_contentProvider.reset();
    delete m_session;
#  ifdef HAVE_QT_PDF
    delete m_renderer;
    delete m_extractor;
#  elif defined(HAVE_POPPLER)
    delete m_renderer;
    // m_popplerDoc (unique_ptr) cleaned up automatically
#  endif
#endif
}

// ── File ──────────────────────────────────────────────────────────────────────

void DocumentView::clearDocument()
{
    cancelCurrentEdit();
    m_selection->clear();

    if (m_viewMode == ViewMode::Grid) {
        m_layoutEngine->clearGrid();
        m_gridCanvas->hide();
        m_gridCanvas->setMinimumHeight(0);
        takeWidget();
        setWidget(m_canvas);
        m_canvas->show();
        m_viewMode = ViewMode::Single;
        Q_EMIT viewModeChanged(ViewMode::Single);
    }
#ifdef HAVE_PDF_RENDERING
    hideHoverHighlight();
    m_contentProvider.reset();
#endif
#ifdef HAVE_QT_PDF
    m_session->clearSuspended();
    m_session->clear();
    m_document->close();
    m_ocrCache.clear();
#elif defined(HAVE_POPPLER)
    m_session->clearSuspended();
    m_session->clear();
    m_ocrCache.clear();
    // Drop the handles into m_renderer before the object behind it dies.
    m_selection->setSource(nullptr, nullptr);
    m_layoutEngine->setSource(nullptr, m_session);
    delete m_renderer;
    m_renderer = nullptr;
    m_popplerDoc.reset();
#endif
    m_filePath.clear();
#ifdef HAVE_PDF_RENDERING
    // m_renderer is already gone on the Poppler path — drop the stale handles.
    m_imageLayer->setSource(m_renderer, m_session, m_ocrEngine, QString());
#endif
    m_pageCount = 0;
    m_lastReportedPage = -1;
    m_undoStack->clear();

    m_layoutEngine->clearPages();

    m_imageLayer->clear();

    m_dropHint->show();
}

bool DocumentView::openFile(const QString &path)
{
    if (path.isEmpty()) return false;
    cancelCurrentEdit();

    if (m_viewMode == ViewMode::Grid)
        setViewMode(ViewMode::Single);

#ifdef HAVE_QT_PDF
    m_session->clear();
    const auto err = m_document->load(path);
    if (err != QPdfDocument::Error::None) return false;
    m_filePath  = path;
    m_pageCount = m_document->pageCount();
    m_imageLayer->setSource(m_renderer, m_session, m_ocrEngine, m_filePath);
    m_layoutEngine->setPageCount(m_pageCount);
    resetContentProvider();
    m_dropHint->hide();
    m_layoutEngine->buildPages();
    Q_EMIT fileOpened(m_filePath, m_pageCount);
    m_lastReportedPage = 0;          // freshly opened documents start at the top
    Q_EMIT pageChanged(1, m_pageCount);
    return true;

#elif defined(HAVE_POPPLER)
    auto doc = Poppler::Document::load(path);
    if (!doc || doc->isLocked()) return false;
    doc->setRenderHint(Poppler::Document::Antialiasing);
    doc->setRenderHint(Poppler::Document::TextAntialiasing);
    m_session->clear();
    m_ocrCache.clear();
    m_contentProvider.reset();   // references the old doc — drop it first
    delete m_renderer;
    m_popplerDoc = std::move(doc);
    m_renderer   = new PdfRenderer(m_popplerDoc.get());
    m_selection->setSource(m_renderer, m_popplerDoc.get());
    m_filePath   = path;
    m_pageCount  = m_popplerDoc->numPages();
    m_imageLayer->setSource(m_renderer, m_session, m_ocrEngine, m_filePath);
    m_layoutEngine->setSource(m_renderer, m_session);
    m_layoutEngine->setPageCount(m_pageCount);
    resetContentProvider();
    m_dropHint->hide();
    m_layoutEngine->buildPages();
    Q_EMIT fileOpened(m_filePath, m_pageCount);
    m_lastReportedPage = 0;          // freshly opened documents start at the top
    Q_EMIT pageChanged(1, m_pageCount);
    return true;

#else
    m_filePath  = path;
    m_pageCount = 1;
    m_dropHint->show();
    retranslateUi();
    Q_EMIT fileOpened(m_filePath, m_pageCount);
    m_lastReportedPage = 0;          // freshly opened documents start at the top
    Q_EMIT pageChanged(1, m_pageCount);
    return true;
#endif
}

// ── Zoom / Tool / Edit mode ───────────────────────────────────────────────────

void DocumentView::setZoom(int percent)
{
    if (m_zoom == percent) return;
    m_zoom = percent;
    m_layoutEngine->setZoom(percent);
    Q_EMIT zoomChanged(percent);
    if (!m_filePath.isEmpty()) m_layoutEngine->rerenderAll();
    // Page labels are re-laid out asynchronously after rerenderAll(), so the
    // highlights are repositioned once more when that layout has settled.
    m_selection->relayout();
    QTimer::singleShot(0, this, [this]() { m_selection->relayout(); });

#ifdef HAVE_PDF_RENDERING
    // rerenderAll() re-renders pages from the PDF without the blank that
    // rerenderPageWithBlank() painted.  Re-apply it so the original text stays
    // hidden behind the editor at the new zoom level.
    if (m_activeEditPage >= 0 && m_editorFrame->isVisible()) {
        if (m_activeEditSourcePage >= 0)
            rerenderPageWithBlank(m_activeEditSourcePage, m_activeEditOriginalBounds);

        // Reposition the editor frame for the new zoom.  The label positions
        // returned by lbl->pos() are stale immediately after rerenderAll()
        // because Qt's layout manager updates geometry asynchronously.
        // A 0 ms timer defers the reposition until after the layout has settled.
        const int activePage = m_activeEditPage;
        QTimer::singleShot(0, this, [this, activePage]() {
            if (m_activeEditPage != activePage || !m_editorFrame->isVisible()) return;
            // Force the canvas layout NOW — label positions are stale until
            // the deferred relayout has run, and the 0 ms timer can fire first.
            if (m_layout) m_layout->activate();
            const QLabel *lbl = pageLabel(activePage);
            if (!lbl) return;
            // Read the CURRENT zoom, not a captured one: rapid wheel zooming
            // queues several of these lambdas and each must position for the
            // zoom the page is actually rendered at.
            const qreal scale = PdfRenderer::screenScale(m_zoom);
            m_editorFrame->setPageRect(lbl->geometry());  // page rect changes with zoom
            const QRectF cb(
                m_activeEditBounds.topLeft() * scale + QPointF(lbl->pos()),
                m_activeEditBounds.size() * scale);
            m_editorFrame->repositionForZoom(
                cb, qMax(6, qRound(m_currentEditorFontSizePt * scale)));
        });
    }
#endif
}

void DocumentView::setZoomSettings(int step, bool ctrlWheel, bool toPointer,
                                   const QString &wheelAction)
{
    m_zoomStep         = step;
    m_ctrlWheelEnabled = ctrlWheel;
    m_zoomToPointer    = toPointer;
    m_wheelAction      = wheelAction;
}

void DocumentView::wheelEvent(QWheelEvent *e)
{
    const bool hasCtrl = e->modifiers() & Qt::ControlModifier;
    const bool shouldZoom = (hasCtrl && m_ctrlWheelEnabled)
                         || (!hasCtrl && m_wheelAction == QLatin1String("zoom"));

    if (shouldZoom) {
        const int delta = e->angleDelta().y();
        if (delta == 0) { QScrollArea::wheelEvent(e); return; }

        const int oldZoom = m_zoom;
        if (delta > 0) setZoom(qMin(m_zoom + m_zoomStep, 300));
        else           setZoom(qMax(m_zoom - m_zoomStep, 25));

        if (m_zoomToPointer && m_zoom != oldZoom) {
            const QPoint pos = viewport()->mapFromGlobal(QCursor::pos());
            const qreal ratio = qreal(m_zoom) / oldZoom;
            horizontalScrollBar()->setValue(
                qRound(ratio * (horizontalScrollBar()->value() + pos.x()) - pos.x()));
            verticalScrollBar()->setValue(
                qRound(ratio * (verticalScrollBar()->value() + pos.y()) - pos.y()));
        }

        e->accept();
        return;
    }

    QScrollArea::wheelEvent(e);
}

void DocumentView::setTool(Tool tool)
{
    if (tool != Tool::Select) m_selection->clear();
    m_tool = tool;

    // Image annotations are interactive only while the image tool is active.
    m_imageLayer->setToolActive(tool == Tool::Image);

    switch (tool) {
    case Tool::Pan:    viewport()->setCursor(Qt::OpenHandCursor);    break;
    case Tool::Text:   viewport()->setCursor(Qt::IBeamCursor);       break;
    case Tool::Select: viewport()->setCursor(Qt::IBeamCursor);       break;   // marks text
    case Tool::Image:
        viewport()->setCursor(Qt::CrossCursor);
        m_imageLayer->scanVisiblePage(firstVisiblePage());
        break;
    default:           viewport()->setCursor(Qt::ArrowCursor);       break;
    }
}

void DocumentView::setEditMode(bool on)
{
    if (m_editMode == on) return;
#ifdef HAVE_PDF_RENDERING
    commitCurrentEdit(m_editorFrame->currentText());
#endif
    hideHoverHighlight();
    m_editMode = on;
    if (!on) setTool(m_tool); // restore tool cursor when leaving edit mode
}

bool DocumentView::saveToFile(const QString &path)
{
#ifdef HAVE_QT_PDF
    commitCurrentEdit(m_editorFrame->currentText());
    if (!m_session->saveToFile(path, m_document, m_pageCount, m_filePath))
        return false;

    // Reload from the saved file so subsequent saves and re-edits work on
    // the updated PDF (with the replacement text) rather than the original.
    m_session->clear();
    m_filePath = path;
    m_document->close();
    m_document->load(path);
    resetContentProvider();
    m_layoutEngine->rerenderAll();
    return true;
#elif defined(HAVE_POPPLER)
    commitCurrentEdit(m_editorFrame->currentText());
    if (!m_popplerDoc || !m_renderer) return false;
    if (!savePopplerRaster(path)) return false;

    // Reload from the saved file so subsequent saves work on the updated content.
    m_session->clear();
    m_filePath = path;
    auto doc = Poppler::Document::load(path);
    if (doc) {
        doc->setRenderHint(Poppler::Document::Antialiasing);
        doc->setRenderHint(Poppler::Document::TextAntialiasing);
        m_contentProvider.reset();   // references the old doc — drop it first
        delete m_renderer;
        m_popplerDoc = std::move(doc);
        m_renderer   = new PdfRenderer(m_popplerDoc.get());
    }
    resetContentProvider();
    m_layoutEngine->rerenderAll();
    return true;
#else
    Q_UNUSED(path)
    return false;
#endif
}

bool DocumentView::hasUnsavedEdits() const
{
#ifdef HAVE_PDF_RENDERING
    return m_session && m_session->hasAnyEdits();
#else
    return false;
#endif
}

#ifdef HAVE_POPPLER
bool DocumentView::savePopplerRaster(const QString &outputPath)
{
    if (!m_popplerDoc || !m_renderer || !m_session || m_pageCount <= 0)
        return false;

    constexpr qreal kPts2Px = 300.0 / 72.0;  // 300 DPI

    const QSizeF firstPts = m_renderer->pageSizePts(0);
    QPdfWriter writer(outputPath);
    writer.setCreator(QStringLiteral("OpenPDF Studio"));
    writer.setResolution(300);
    writer.setPageSize(QPageSize(firstPts, QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));

    QPainter painter(&writer);
    if (!painter.isActive()) return false;

    for (int i = 0; i < m_pageCount; ++i) {
        if (i > 0 && !writer.newPage()) return false;
        QImage img = m_renderer->renderPage(i, kPts2Px);
        if (img.isNull()) continue;
        m_session->applyToImage(i, img, kPts2Px);
        const QRect pageRect(0, 0, painter.device()->width(), painter.device()->height());
        painter.drawImage(pageRect, img);
    }
    painter.end();
    return true;
}
#endif // HAVE_POPPLER

void DocumentView::resetContentProvider()
{
#ifdef HAVE_PDF_RENDERING
    m_contentProvider.reset();
    // The document may have been reloaded from disk.
    m_selection->invalidateCaches();
#  if defined(HAVE_QT_PDF) && defined(HAVE_QPDF)
    if (!m_filePath.isEmpty())
        m_contentProvider = std::make_unique<QpdfContentProvider>(
            m_filePath,
            [this](int p) { return m_document->pagePointSize(p); });
#  elif defined(HAVE_POPPLER)
    if (m_popplerDoc)
        m_contentProvider = std::make_unique<PopplerContentProvider>(m_popplerDoc.get());
#  endif
#endif
}

// ── Text selection (Select tool) ─────────────────────────────────────────────
// Lives in TextSelectionController (ui/view/). Works in normal mode and in edit
// mode: the select tool only ever reads text, it never touches the session.

QString DocumentView::selectedText() const
{
    return m_selection->selectedText();
}

void DocumentView::copySelectedText()
{
    m_selection->copyToClipboard();
}

// ── Hover highlight (edit mode) ───────────────────────────────────────────────

void DocumentView::hideHoverHighlight()
{
#ifdef HAVE_PDF_RENDERING
    if (m_hoverHighlight) m_hoverHighlight->hide();
    m_hoverPage   = -1;
    m_hoverBounds = QRectF();
#endif
}

void DocumentView::updateHoverHighlight(const QPoint &canvasPos)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_contentProvider || !m_hoverHighlight) return;

    // Don't fight the open editor for the same area.
    if (m_editorFrame && m_editorFrame->isVisible()
            && m_editorFrame->geometry().contains(canvasPos)) {
        hideHoverHighlight();
        return;
    }

    auto [pageIdx, pageLbl] = pageAtCanvasPos(canvasPos);
    if (pageIdx < 0) { hideHoverHighlight(); return; }

    const qreal   scale = PdfRenderer::screenScale(m_zoom);
    const QPointF pdfPt = QPointF(canvasPos - pageLbl->pos()) / scale;

    // Hover must NEVER build the page model — that parses the whole file on
    // the UI thread and freezes scrolling (Qt synthesizes mouse moves while
    // scrolling). Only pages already built by a click get hover feedback.
    if (!m_contentProvider->hasPage(pageIdx)) {
        hideHoverHighlight();
        return;
    }

    // Blanked areas (source of a moved/deleted block) are intentionally empty —
    // highlighting the visually erased native text there would be misleading.
    if (m_session && m_session->isBlankAt(pageIdx, pdfPt)) {
        hideHoverHighlight();
        return;
    }

    // Exact hits only (6 pt slack) — nearest-match hover feels jumpy.
    const ContentItem item = m_contentProvider->itemAt(
        pageIdx, pdfPt, kAllContentTypes, 6.0);
    if (!item.isValid()) { hideHoverHighlight(); return; }

    // Items that are mostly erased (source of a moved block) aren't there
    // anymore — don't advertise them.
    if (m_session && m_session->isBlankCovering(pageIdx, item.bounds)) {
        hideHoverHighlight();
        return;
    }

    if (pageIdx == m_hoverPage && item.bounds == m_hoverBounds
            && m_hoverHighlight->isVisible())
        return;
    m_hoverPage   = pageIdx;
    m_hoverBounds = item.bounds;

    const char *border = "#3B82F6";                       // Text/Paragraph: blue
    QString label      = tr("Text");
    switch (item.type) {
    case ContentItem::Type::Paragraph: label = tr("Paragraph");                     break;
    case ContentItem::Type::TableCell: label = tr("Table cell"); border = "#10B981"; break;
    case ContentItem::Type::FormField: label = tr("Form field"); border = "#F59E0B"; break;
    case ContentItem::Type::Image:     label = tr("Image");      border = "#8B5CF6"; break;
    case ContentItem::Type::Media:     label = tr("Media");      border = "#EF4444"; break;
    default: break;
    }

    QString tip = label;
    if (!item.fontFamily.isEmpty() && item.fontSizePt > 0.0)
        tip += QStringLiteral(" — %1 %2 pt").arg(item.fontFamily)
                   .arg(qRound(item.fontSizePt));
    else if (item.fontSizePt > 0.0)
        tip += QStringLiteral(" — %1 pt").arg(qRound(item.fontSizePt));
    if (!item.fieldName.isEmpty())
        tip += QStringLiteral(" (%1)").arg(item.fieldName);

    m_hoverHighlight->setStyleSheet(QStringLiteral(
        "QFrame { border: 1px dashed %1; border-radius: 2px;"
        " background: transparent; }").arg(QLatin1String(border)));
    m_hoverHighlight->setToolTip(tip);

    const QRectF canvasRect(item.bounds.topLeft() * scale + QPointF(pageLbl->pos()),
                            item.bounds.size() * scale);
    m_hoverHighlight->setGeometry(canvasRect.toAlignedRect().adjusted(-2, -2, 2, 2));
    m_hoverHighlight->raise();
    m_hoverHighlight->show();
#else
    Q_UNUSED(canvasPos)
#endif
}

// ── Editor font state (FormatBar sync) ────────────────────────────────────────

void DocumentView::refreshEditorFontLive()
{
#ifdef HAVE_PDF_RENDERING
    if (m_activeEditPage < 0 || !m_editorFrame->isVisible()) return;
    m_editorFrame->setTextFont(m_currentEditorFontFamily,
                               m_currentEditorBold, m_currentEditorItalic);
#endif
}

void DocumentView::setEditorFontFamily(const QString &family)
{
#ifdef HAVE_PDF_RENDERING
    if (family.isEmpty() || family == m_currentEditorFontFamily) return;
    m_currentEditorFontFamily = family;
    m_editorFontChangedByUser = true;
    refreshEditorFontLive();
#else
    Q_UNUSED(family)
#endif
}

void DocumentView::setEditorBold(bool on)
{
#ifdef HAVE_PDF_RENDERING
    if (on == m_currentEditorBold) return;
    m_currentEditorBold       = on;
    m_editorFontChangedByUser = true;
    refreshEditorFontLive();
#else
    Q_UNUSED(on)
#endif
}

void DocumentView::setEditorItalic(bool on)
{
#ifdef HAVE_PDF_RENDERING
    if (on == m_currentEditorItalic) return;
    m_currentEditorItalic     = on;
    m_editorFontChangedByUser = true;
    refreshEditorFontLive();
#else
    Q_UNUSED(on)
#endif
}

void DocumentView::setEditorFontSize(int ptSize)
{
#ifdef HAVE_PDF_RENDERING
    if (ptSize < 4 || ptSize > 400) return;
    if (m_activeEditPage >= 0 && m_editorFrame->isVisible()) {
        const qreal scale = PdfRenderer::screenScale(m_zoom);
        // Scale the edit bounds proportionally so line-wrapping is preserved.
        // Both width and height scale with the font ratio so character counts
        // per line stay the same and formatting doesn't change.
        if (m_currentEditorFontSizePt > 0 && ptSize != m_currentEditorFontSizePt) {
            const qreal ratio = (qreal)ptSize / m_currentEditorFontSizePt;
            const QPointF anchor = m_activeEditBounds.topLeft();
            m_activeEditBounds = QRectF(anchor, m_activeEditBounds.size() * ratio);
            clampToPdfPage(m_activeEditPage, m_activeEditBounds);
        }
        m_currentEditorFontSizePt = ptSize;
        const QLabel *lbl = pageLabel(m_activeEditPage);
        if (lbl) {
            m_editorFrame->setPageRect(lbl->geometry());
            const QRectF cb(
                m_activeEditBounds.topLeft() * scale + QPointF(lbl->pos()),
                m_activeEditBounds.size() * scale);
            m_editorFrame->repositionForZoom(cb, qMax(6, qRound(ptSize * scale)));
        }
    } else {
        m_currentEditorFontSizePt = ptSize;
    }
#else
    Q_UNUSED(ptSize)
#endif
}

void DocumentView::setEditorTextColor(const QColor &color)
{
#ifdef HAVE_PDF_RENDERING
    if (!color.isValid()) return;
    m_currentEditorColor = color;
    if (m_activeEditPage >= 0 && m_editorFrame->isVisible())
        m_editorFrame->setTextColor(color);
#else
    Q_UNUSED(color)
#endif
}

int DocumentView::currentPage() const
{
    if (pageLabelCount() == 0) return 0;

    // The page covering most of the viewport is the one the user is reading.
    // Taking the first partially visible page instead would keep the indicator
    // one page behind for as long as a sliver of it hangs in at the top.
    const int top    = verticalScrollBar()->value();
    const int bottom = top + viewport()->height();

    int best = 0;
    int bestVisible = -1;
    for (int i = 0; i < pageLabelCount(); ++i) {
        const int y0 = pageLabel(i)->pos().y();
        const int y1 = y0 + pageLabel(i)->height();
        if (y0 >= bottom) break;

        const int visible = qMin(y1, bottom) - qMax(y0, top);
        if (visible > bestVisible) {
            bestVisible = visible;
            best        = i;
        }
    }
    return best;
}

int DocumentView::firstVisiblePage() const
{
    for (int i = 0; i < pageLabelCount(); ++i)
        if (pageLabel(i)->pos().y() >= verticalScrollBar()->value())
            return i;
    return 0;
}

void DocumentView::goToPage(int page)
{
    scrollToPage(page, true);
}

void DocumentView::scrollToPage(int page, bool allowRetry)
{
    if (pageLabelCount() == 0) return;
    page = qBound(0, page, pageLabelCount() - 1);

    // The page widgets live on m_canvas, which is not the widget on screen in
    // grid mode — jumping to a page there means returning to the page view.
    if (m_viewMode == ViewMode::Grid)
        setViewMode(ViewMode::Single);

    // Right after open/zoom the layout can still be pending, in which case
    // every page would report pos().y() == 0 and the jump would go nowhere.
    if (m_layout) m_layout->activate();

    constexpr int kTopGap = 20;   // leave a little air above the page
    const int target = qMax(0, pageLabel(page)->pos().y() - kTopGap);
    verticalScrollBar()->setValue(target);

    if (allowRetry && verticalScrollBar()->value() != target) {
        // The scroll area sizes its canvas — and with it the scrollbar range —
        // only once it has processed the new page widgets. Until then the jump
        // is clamped to 0, which is what made the very first click on the page
        // arrows after opening a document do nothing. Retry once, after that.
        QTimer::singleShot(0, this, [this, page]() { scrollToPage(page, false); });
        return;
    }
    reportCurrentPage();
}

void DocumentView::reportCurrentPage()
{
    if (m_pageCount <= 0 || pageLabelCount() == 0) return;
    const int page = currentPage();
    if (page == m_lastReportedPage) return;
    m_lastReportedPage = page;
    Q_EMIT pageChanged(page + 1, m_pageCount);
}

bool DocumentView::pdfRenderingAvailable() const
{
#ifdef HAVE_PDF_RENDERING
    return true;
#else
    return false;
#endif
}

QList<DocxPage> DocumentView::allPageContent()
{
    QList<DocxPage> result;
#ifdef HAVE_PDF_RENDERING
    if (!m_renderer || m_pageCount <= 0) return result;
    result.reserve(m_pageCount);
    for (int i = 0; i < m_pageCount; ++i) {
        DocxPage page;
        page.pageSizePt = m_renderer->pageSizePts(i);
        if (m_contentProvider)
            page.items = m_contentProvider->pageItems(i);
#  ifdef HAVE_QT_PDF
        // qpdf exposes raw string bytes. For embedded fonts with a custom
        // encoding those bytes are character codes, not Unicode. Build the
        // complete text model from Qt's decoded polygons instead; qpdf items
        // remain useful only as nearby style/colour metadata sources.
        if (m_document) {
            const QList<ContentItem> detected = page.items;
            const QPdfSelection all = m_document->getAllText(i);
            QList<ContentCluster> clusters;
            if (all.isValid()) {
                for (const QPolygonF &polygon : all.bounds()) {
                    const QRectF rect = polygon.boundingRect();
                    if (rect.isEmpty()) continue;
                    const QRectF query = rect;
                    const QPdfSelection selection = m_document->getSelection(
                        i, query.topLeft(), query.bottomRight());
                    QString text = selection.text();
                    // Qt may return the queried visual line plus a fragment of
                    // the following line when selection polygons touch. One
                    // polygon represents exactly one visual line here, so keep
                    // only that first line and discard PDF non-characters.
                    // Qt uses U+FFFE at a line-end discretionary hyphen in
                    // several Writer-generated PDFs. It is visually a '-' and
                    // must not silently join words such as "Hardware-Lifecycle".
                    text.replace(QChar(0xFFFE), QStringLiteral("-"));
                    text.replace(QChar(0xFFFF), QString{});
                    text = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                      Qt::SkipEmptyParts).value(0).trimmed();
                    // OpenSymbol bullet polygons intentionally have no Unicode
                    // selection text in Qt although the page-wide text stream
                    // contains U+2022. Their tiny square geometry is unambiguous.
                    if (text.isEmpty() && rect.width() <= 6.0 && rect.height() <= 6.0)
                        text = QStringLiteral("•");
                    if (text.isEmpty()) continue;

                    ContentCluster cluster;
                    cluster.bounds = rect;
                    cluster.text = text;
                    cluster.fontSizePt = qMax(2.0, rect.height() * 0.74);
                    cluster.exactWidth = true;

                    const ContentItem *style = nullptr;
                    double bestDistance = 1e18;
                    for (const ContentItem &candidate : detected) {
                        if (!candidate.isTextual()) continue;
                        const double distance = QLineF(candidate.bounds.center(),
                                                       rect.center()).length();
                        if (distance < bestDistance) {
                            bestDistance = distance;
                            style = &candidate;
                        }
                    }
                    if (style) {
                        cluster.rawFontName = style->rawFontName;
                        cluster.textColor = style->textColor;
                        if (style->fontSizePt > 0.0)
                            cluster.fontSizePt = qMin(style->fontSizePt,
                                                      rect.height() * 0.82);
                    }
                    clusters.append(std::move(cluster));
                }
            }
            // Keep one export item per visual PDF line/cell. Vertical merging
            // is useful for the editor, but Word's line spacing would move
            // merged table rows away from the original raster grid.
            QList<ContentItem> decoded = classifyContentClusters(
                std::move(clusters), false);
            if (!decoded.isEmpty()) {
                // The classifier resolves font style from rawFontName. Copy the
                // nearest detected fill explicitly because it is page-paint
                // metadata rather than a property of Qt's text polygons.
                for (ContentItem &item : decoded) {
                    const ContentItem *style = nullptr;
                    double bestDistance = 1e18;
                    for (const ContentItem &candidate : detected) {
                        if (!candidate.isTextual()) continue;
                        const double distance = QLineF(candidate.bounds.center(),
                                                       item.bounds.center()).length();
                        if (distance < bestDistance) {
                            bestDistance = distance;
                            style = &candidate;
                        }
                    }
                    if (style) item.bgColor = style->bgColor;
                }

                // Some PDFs have no usable ToUnicode map: Qt renders the glyphs
                // correctly but extraction returns Greek/symbol characters for
                // German prose. Detect that signature and OCR only those lines.
                bool needsOcr = false;
                for (const ContentItem &item : decoded) {
                    int greek = 0, letters = 0;
                    for (const QChar c : item.text) {
                        if (!c.isLetter()) continue;
                        ++letters;
                        const ushort u = c.unicode();
                        if ((u >= 0x0370 && u <= 0x03FF)
                                || (u >= 0x1F00 && u <= 0x1FFF))
                            ++greek;
                    }
                    if (letters >= 4 && greek * 4 >= letters) {
                        needsOcr = true;
                        break;
                    }
                }
                if (needsOcr && m_ocrEngine && m_ocrEngine->isReady()) {
                    constexpr qreal ocrScale = 2.5;
                    const QImage ocrImage = m_renderer->renderPage(i, ocrScale);
                    const QList<OcrEngine::Block> blocks = m_ocrEngine->recognizePage(
                        ocrImage, page.pageSizePt, ocrScale);
                    if (!blocks.isEmpty()) {
                        QList<ContentItem> ocrItems;
                        ocrItems.reserve(blocks.size());
                        for (const OcrEngine::Block &block : blocks) {
                            if (!block.isValid()) continue;
                            ContentItem item;
                            item.type = ContentItem::Type::Text;
                            item.bounds = block.pdfBounds;
                            item.text = block.text;
                            item.fontSizePt = qMax(2.0, block.pdfBounds.height() * 0.72);

                            const ContentItem *style = nullptr;
                            double bestDistance = 1e18;
                            for (const ContentItem &candidate : detected) {
                                if (!candidate.isTextual()) continue;
                                const double distance = QLineF(candidate.bounds.center(),
                                                               item.bounds.center()).length();
                                if (distance < bestDistance) {
                                    bestDistance = distance;
                                    style = &candidate;
                                }
                            }
                            if (style) {
                                item.fontFamily = style->fontFamily;
                                item.rawFontName = style->rawFontName;
                                item.bold = style->bold;
                                item.italic = style->italic;
                                item.textColor = style->textColor;
                                item.bgColor = style->bgColor;
                            }
                            ocrItems.append(std::move(item));
                        }
                        if (!ocrItems.isEmpty()) decoded = std::move(ocrItems);
                    }
                }
                for (const ContentItem &item : detected)
                    if (!item.isTextual()) decoded.append(item);
                page.items = std::move(decoded);
            }
        }
        if (page.items.isEmpty() && m_document) {
            ContentItem item;
            item.type = ContentItem::Type::Paragraph;
            item.bounds = QRectF(54.0, 54.0,
                                 qMax(1.0, page.pageSizePt.width() - 108.0),
                                 qMax(1.0, page.pageSizePt.height() - 108.0));
            item.text = m_document->getAllText(i).text();
            item.fontSizePt = 11.0;
            if (!item.text.trimmed().isEmpty()) page.items.append(item);
        }
#  endif
        // Preserve images/vector graphics as a raster layer, then remove the
        // native PDF glyphs at their exact renderer-reported rectangles. DOCX
        // text boxes are placed over this cleaned layer and remain editable.
        constexpr qreal exportScale = 2.0;
        page.background = m_renderer->renderPage(i, exportScale);
        if (!page.background.isNull()) {
            QPainter painter(&page.background);
            painter.setRenderHint(QPainter::Antialiasing, false);
            if (m_session) {
                for (const EditSession::ImageEdit &edit : m_session->imageEdits()) {
                    if (edit.page != i || edit.image.isNull()) continue;
                    painter.drawImage(QRectF(edit.pdfBounds.topLeft() * exportScale,
                                             edit.pdfBounds.size() * exportScale),
                                      edit.image);
                }
            }
            for (const ContentItem &item : page.items) {
                if (!item.isTextual() || item.text.trimmed().isEmpty()) continue;
                QList<QRectF> eraseRects;
#  ifdef HAVE_QT_PDF
                if (m_extractor)
                    eraseRects = m_extractor->glyphRects(i, item.bounds, {});
#  endif
                if (eraseRects.isEmpty()) eraseRects.append(item.bounds);

                QColor itemBg = item.bgColor;
                if (!itemBg.isValid()) {
                    const QPointF probePt((item.bounds.left() - 2.0) * exportScale,
                                          item.bounds.center().y() * exportScale);
                    const int x = qBound(0, qRound(probePt.x()),
                                         page.background.width() - 1);
                    const int y = qBound(0, qRound(probePt.y()),
                                         page.background.height() - 1);
                    itemBg = page.background.pixelColor(x, y);
                    // A dark probe is almost certainly the preceding glyph or
                    // a cell rule, not the page/cell background.
                    if (itemBg.lightness() < 45 && (!item.textColor.isValid()
                                                    || item.textColor.lightness() < 180))
                        itemBg = Qt::white;
                }

                // Join exact word/glyph boxes per visual line. ContentItem width
                // is estimated by the qpdf scanner and can extend far past the
                // last word; filling that estimate creates the visible colour
                // bars. Line unions retain the stronger duplicate-text cleanup
                // while stopping at the actual rendered line end.
                QList<QRectF> lineRects;
                for (const QRectF &rect : eraseRects) {
                    int host = -1;
                    for (int line = 0; line < lineRects.size(); ++line) {
                        const double tolerance = qMax(lineRects[line].height(),
                                                      rect.height()) * 0.60;
                        if (qAbs(lineRects[line].center().y() - rect.center().y())
                                <= tolerance) {
                            host = line;
                            break;
                        }
                    }
                    if (host >= 0)
                        lineRects[host] = lineRects[host].united(rect);
                    else
                        lineRects.append(rect);
                }
                for (const QRectF &line : lineRects) {
                    const QRectF cleanLine = line.adjusted(-0.35, -0.25, 0.75, 0.25);
                    painter.fillRect(QRectF(cleanLine.topLeft() * exportScale,
                                            cleanLine.size() * exportScale), itemBg);
                }

                for (const QRectF &rect : eraseRects) {
                    // Qt's polygons hug the visible glyphs. Half a point covers
                    // antialiasing fringes without crossing table borders.
                    const QRectF clean = rect.adjusted(-0.55, -0.45, 0.55, 0.45);
                    painter.fillRect(QRectF(clean.topLeft() * exportScale,
                                            clean.size() * exportScale), itemBg);
                }
            }
        }
        result.append(std::move(page));
    }
#endif
    return result;
}

bool DocumentView::exportPagesToImages(const QString &outputPath, int quality)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_renderer || m_pageCount <= 0 || outputPath.isEmpty()) return false;
    const QFileInfo out(outputPath);
    const qreal scale = quality >= 95 ? 3.0 : quality >= 80 ? 2.0
                                      : quality >= 55 ? 1.5 : 1.0;
    for (int i = 0; i < m_pageCount; ++i) {
        QImage image = m_renderer->renderPage(i, scale);
        if (image.isNull()) return false;
        if (m_session) m_session->applyToImage(i, image, scale);
        const QString path = m_pageCount == 1
            ? outputPath
            : out.dir().filePath(out.completeBaseName()
                                 + QStringLiteral("_page_%1.png").arg(i + 1));
        if (!image.save(path, "PNG", qBound(0, quality, 100))) return false;
    }
    return true;
#else
    Q_UNUSED(outputPath);
    Q_UNUSED(quality);
    return false;
#endif
}

void DocumentView::retranslateUi()
{
#ifdef HAVE_PDF_RENDERING
    m_dropHint->setText(tr("Drop a PDF here or click a tab to open"));
#else
    m_dropHint->setText(tr(
        "PDF rendering is not available in this build.\n"
        "Please use a build that includes Qt6::Pdf support.\n"
        "(See build-win.sh for instructions.)"));
#endif
}

void DocumentView::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QScrollArea::changeEvent(e);
}

void DocumentView::keyPressEvent(QKeyEvent *e)
{
    // Copy marked page text. Editors are QTextEdit children and consume their
    // own Ctrl+C before it ever reaches the view, so both paths coexist.
    if (e->matches(QKeySequence::Copy) && m_selection->hasSelection()) {
        copySelectedText();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Escape && m_selection->hasSelection()) {
        m_selection->clear();
        e->accept();
        return;
    }
    QScrollArea::keyPressEvent(e);
}

void DocumentView::resizeEvent(QResizeEvent *e)
{
    QScrollArea::resizeEvent(e);
    if (m_viewMode == ViewMode::Grid)
        m_layoutEngine->relayoutGrid(viewport()->width());
}

// ── Page rendering (delegated to PageLayoutEngine) ────────────────────────────

QLabel *DocumentView::pageLabel(int page) const
{
    return m_layoutEngine->pageLabel(page);
}

int DocumentView::pageLabelCount() const
{
    return m_layoutEngine->pageLabelCount();
}

void DocumentView::rerenderPage(int page)
{
    m_layoutEngine->rerenderPage(page);
}

void DocumentView::rerenderPageWithBlank(int page, const QRectF &pdfBoundsPts)
{
#ifdef HAVE_PDF_RENDERING
    m_layoutEngine->rerenderPageWithBlank(page, pdfBoundsPts, m_activeEditEraseRects);
#else
    Q_UNUSED(page) Q_UNUSED(pdfBoundsPts)
#endif
}

// ── Grid view ─────────────────────────────────────────────────────────────────

void DocumentView::setViewMode(ViewMode mode)
{
    if (m_viewMode == mode) return;
    m_selection->clear();   // grid view has no page-text geometry to anchor to
    m_viewMode = mode;

    if (mode == ViewMode::Grid) {
        if (m_pageCount == 0) { m_viewMode = ViewMode::Single; return; }
        m_layoutEngine->buildGridItems();
        m_canvas->hide();
        takeWidget();
        setWidget(m_gridCanvas);
        m_gridCanvas->show();
        // Defer: viewport()->width() is reliable after the scroll area processes
        // the new widget.
        QMetaObject::invokeMethod(this, [this]() {
            m_layoutEngine->relayoutGrid(viewport()->width());
        }, Qt::QueuedConnection);
    } else {
        m_layoutEngine->clearGrid();
        m_gridCanvas->hide();
        m_gridCanvas->setMinimumHeight(0);
        takeWidget();
        setWidget(m_canvas);
        m_canvas->show();
    }
    Q_EMIT viewModeChanged(mode);
}

// ── Context menu (editor / page selection) ────────────────────────────────────

void DocumentView::showGeneralContextMenu(const QPoint &globalPos)
{
    auto *focusEdit    = qobject_cast<QTextEdit *>(QApplication::focusWidget());
    const bool hasEdit = focusEdit && m_editorFrame && m_editorFrame->isVisible();
    const bool hasSel  = hasEdit && focusEdit->textCursor().hasSelection();
    // Text marked on the page with the select tool — copy only, the page text
    // itself is not modified from here.
    const bool hasPageSel = !hasEdit && !selectedText().isEmpty();

    QMenu menu(this);
    QAction *copy = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")),
                                   tr("Kopieren"));
    copy->setEnabled(hasSel || hasPageSel);

    QAction *act = menu.exec(globalPos);
    if (act != copy) return;

    if (hasPageSel)      copySelectedText();
    else if (focusEdit)  focusEdit->copy();
}

// ── Edit mode ─────────────────────────────────────────────────────────────────

qreal DocumentView::screenScale() const
{
#ifdef HAVE_PDF_RENDERING
    return PdfRenderer::screenScale(m_zoom);
#else
    return m_zoom / 100.0;
#endif
}

std::pair<int, QLabel *> DocumentView::pageAtCanvasPos(const QPoint &canvasPos) const
{
    for (int i = 0; i < pageLabelCount(); ++i)
        if (pageLabel(i)->geometry().contains(canvasPos))
            return { i, pageLabel(i) };
    return { -1, nullptr };
}

void DocumentView::clampToPdfPage(int page, QRectF &r) const
{
#ifdef HAVE_PDF_RENDERING
    if (!m_renderer || page < 0) return;
    const QSizeF ps = m_renderer->pageSizePts(page);
    // Shrink if larger than page
    if (r.width()  > ps.width())  r.setWidth(ps.width());
    if (r.height() > ps.height()) r.setHeight(ps.height());
    // Push inside page boundaries
    if (r.left()   < 0)           r.moveLeft(0);
    if (r.top()    < 0)           r.moveTop(0);
    if (r.right()  > ps.width())  r.moveRight(ps.width());
    if (r.bottom() > ps.height()) r.moveBottom(ps.height());
#else
    Q_UNUSED(page) Q_UNUSED(r)
#endif
}

// Sample the most common non-white pixel color in a region of a rendered page image.
// Returns a fallback near-black if nothing non-white is found.
// Qt PDF renders onto a TRANSPARENT background — the "paper" reads as
// rgba(0,0,0,0). Composite over white before interpreting any pixel, or
// blank paper counts as pitch black.
static inline QRgb pixelOverWhite(const QImage &img, int x, int y)
{
    const QRgb c = img.pixel(x, y);
    const int  a = qAlpha(c);
    if (a == 255) return c;
    return qRgb((qRed(c)   * a + 255 * (255 - a)) / 255,
                (qGreen(c) * a + 255 * (255 - a)) / 255,
                (qBlue(c)  * a + 255 * (255 - a)) / 255);
}

// Text color = the color of the GLYPH CORES, i.e. the darkest percentile of
// the region. A most-frequent-non-white heuristic breaks on backends whose
// antialiasing dominates (thin fonts at low dpi render mostly light grey and
// the mode lands on a wash-out — the committed text then paints ghostly).
static QColor sampleTextColor(const QImage &img, const QRect &region)
{
    const QRect sr = region.intersected(img.rect());
    if (sr.isEmpty()) return QColor(0x11, 0x11, 0x11);

    struct Px { int lum; QRgb rgb; };
    std::vector<Px> pixels;
    pixels.reserve(size_t(sr.width()) * sr.height() / 4 + 16);
    for (int y = sr.top(); y <= sr.bottom(); ++y)
        for (int x = sr.left(); x <= sr.right(); ++x) {
            const QRgb c  = pixelOverWhite(img, x, y);
            const int lum = (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114)
                          / 1000;
            if (lum < 240) pixels.push_back({ lum, c });
        }
    if (pixels.empty()) return QColor(0x11, 0x11, 0x11);

    std::sort(pixels.begin(), pixels.end(),
              [](const Px &a, const Px &b) { return a.lum < b.lum; });
    const size_t take = std::max<size_t>(8, pixels.size() / 10);
    long r = 0, g = 0, b = 0;
    const size_t n = std::min(take, pixels.size());
    for (size_t i = 0; i < n; ++i) {
        r += qRed(pixels[i].rgb);
        g += qGreen(pixels[i].rgb);
        b += qBlue(pixels[i].rgb);
    }
    const QColor core(int(r / n), int(g / n), int(b / n));
    // Nothing dark at all → no real text under the rect; default ink.
    if (core.lightnessF() > 0.82) return QColor(0x11, 0x11, 0x11);
    return core;
}

// Dominant color INSIDE the text bounds — that is the background the blank
// fill must reproduce. Inside a text rect the background always outweighs the
// glyph pixels by area, so the mode is the background color; this holds for
// white pages, colored table rows, and dark headers with light text alike.
// (A ring AROUND the bounds is unusable: for table cells it runs exactly
// along the dark border lines and returns the border color.)
// Works on every backend, including backgrounds the content-stream scan
// can't see (form XObjects, shadings, images).
static QColor sampleBackgroundColor(const QImage &img, const QRect &region)
{
    const QRect sr = region.intersected(img.rect());
    if (sr.isEmpty()) return {};

    QMap<QRgb, int> counts;
    const int stepX = qMax(1, sr.width()  / 120);
    const int stepY = qMax(1, sr.height() / 40);
    for (int y = sr.top(); y <= sr.bottom(); y += stepY)
        for (int x = sr.left(); x <= sr.right(); x += stepX)
            counts[pixelOverWhite(img, x, y)]++;
    if (counts.isEmpty()) return {};

    QRgb best = 0; int bestN = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() > bestN) { bestN = it.value(); best = it.key(); }
    return QColor::fromRgb(best);
}

void DocumentView::handleEditClick(const QPoint &canvasPos)
{
#ifdef HAVE_PDF_RENDERING
    qWarning() << "[EDIT] handleEditClick canvasPos=" << canvasPos;
    auto [pageIdx, pageLbl] = pageAtCanvasPos(canvasPos);
    if (pageIdx < 0) return;

    // Convert canvas position to PDF-point coordinates
    const qreal   scale     = PdfRenderer::screenScale(m_zoom);
    const QPointF pageLocal = QPointF(canvasPos - pageLbl->pos());
    const QPointF pdfPt     = pageLocal / scale;
    const QSizeF  pageSize  = m_renderer->pageSizePts(pageIdx);

    // Session edits take priority over native PDF text.  If the user placed a
    // text box (createTextFrame) or edited text in-place (handleEditClick), their
    // session content is shown on top and clicking that area edits the session
    // content, not the original PDF text underneath.
    // Session-erased areas: text lookup must treat them as gone, or a click
    // near them resurrects the invisible original as a duplicate.
    const QList<QRectF> erasedZones = m_session->blankRegions(pageIdx);

    bool              isSessionEdit = false;
    EditSession::Edit sessionEdit;
    TextBlock block;
    if (m_session->findEditAt(pageIdx, pdfPt, &sessionEdit)) {
        block         = TextBlock{ pageIdx, sessionEdit.pdfBounds, sessionEdit.newText };
        isSessionEdit = true;
    }

    // Region model for the clicked page (cached): exact bounds, font, colors,
    // paragraph/table structure, and fillable form fields.
    ContentItem contentItem;
    if (!isSessionEdit && m_contentProvider)
        contentItem = m_contentProvider->itemAt(pageIdx, pdfPt);

    // Clicking an image/media region with the text tool must not snap to text
    // up to 40 pt away — swallow the click instead (matches the hover UI).
    // Exception: a near-full-page image is a scanned page; those fall through
    // so the OCR path below can offer text editing.
    if (!isSessionEdit && !contentItem.isValid() && m_contentProvider) {
        const ContentItem nonText = m_contentProvider->itemAt(
            pageIdx, pdfPt,
            contentTypeBit(ContentItem::Type::Image)
                | contentTypeBit(ContentItem::Type::Media),
            0.0);
        if (nonText.isValid()) {
            const double pageArea = pageSize.width() * pageSize.height();
            const double itemArea = nonText.bounds.width()
                                  * nonText.bounds.height();
            if (pageArea > 0.0 && itemArea / pageArea < 0.8)
                return;
        }
    }

    // Fillable form field: open the editor on the field bounds even when the
    // field is empty — there is no text for the extractor to find.
    if (!isSessionEdit && contentItem.isFormField())
        block = TextBlock{ pageIdx, contentItem.bounds, contentItem.text };

    // Fall back to native PDF text (vector PDFs)
    if (!block.isValid()) {
#ifdef HAVE_QT_PDF
        block = m_extractor->textAt(pageIdx, pdfPt, pageSize, erasedZones);
#elif defined(HAVE_POPPLER)
        if (m_popplerDoc)
            block = popplerTextAt(m_popplerDoc.get(), pageIdx, pdfPt, erasedZones);
#endif
    }

    // If still nothing, fall back to OCR (scanned / image PDF)
    if (!block.isValid() && m_ocrEngine && m_ocrEngine->isReady()) {
        if (!m_ocrCache.contains(pageIdx)) {
            const qreal ocrScale = qMax(scale * devicePixelRatioF(), 300.0 / 72.0);
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            const QImage pageImg = m_renderer->renderPage(pageIdx, ocrScale);
            m_ocrCache[pageIdx] = m_ocrEngine->recognizePage(pageImg, pageSize, ocrScale);
            QApplication::restoreOverrideCursor();
        }
        const OcrEngine::Block ocr = OcrEngine::blockAt(m_ocrCache[pageIdx], pdfPt);
        if (ocr.isValid())
            block = TextBlock{ pageIdx, ocr.pdfBounds, ocr.text };
    }

    // Last resort: content-stream text of the detected region. Encoding can be
    // imperfect for exotic embedded fonts, but it beats finding nothing.
    if (!block.isValid() && contentItem.isValid() && contentItem.isTextual()
            && !contentItem.text.isEmpty())
        block = TextBlock{ pageIdx, contentItem.bounds, contentItem.text };

    qWarning() << "[EDIT] block valid=" << block.isValid() << "isSession=" << isSessionEdit
               << "itemType=" << int(contentItem.type) << "text=" << block.text.left(40);

    if (!block.isValid()) return;

    // If this click was the release of a press that committed an edit on this
    // same block, don't re-open — that would call rerenderPageWithBlank and
    // erase the text we just committed.
    if (block.page == m_lastCommittedPage &&
        block.pdfBounds.intersects(m_lastCommittedOrigBounds)) {
        m_lastCommittedPage = -1;
        return;
    }
    m_lastCommittedPage = -1;

    // If the click lands inside a blank (erase-only) session edit — the source
    // area of a drag-move — don't open any editor.  The area is intentionally
    // empty; pre-filling with the (still present, only visually erased) native
    // PDF text would duplicate it: native text back at P1 alongside the moved
    // text at P2.  The user can drag-to-create a new text frame here instead.
    // Two guards: the click point itself, AND the found block — text lookup is
    // fuzzy (nearest within 40 pt), so a click NEAR the blanked area can snap
    // onto the invisible original even though the point lies outside the blank.
    // NEVER swallow session-edit clicks: moved text often still overlaps its
    // own blank, and its visible content must stay editable on top of it.
    if (!isSessionEdit && (m_session->isBlankAt(block.page, pdfPt)
                           || m_session->isBlankCovering(block.page, block.pdfBounds)))
        return;

    QString displayText = block.text;

    m_activeEditPage           = block.page;
    m_activeEditSourcePage     = block.page;
    m_activeEditBounds         = block.pdfBounds;
    clampToPdfPage(block.page, m_activeEditBounds);
    m_activeEditOriginalBounds = m_activeEditBounds;
    m_activeEditNeedsBlank     = true;   // editing existing text → must erase original
    m_activeEditFieldName.clear();

    // Apply the region model: bounds, paragraph text, form-field mode.
    // Bounds priority: glyph-accurate extractor rects win over the region
    // model's estimated character widths (estimates overpaint surroundings);
    // the region model takes over when the extractor found nothing or
    // returned a suspicious multi-row rect.
    bool paragraphBounds = false;
    if (!isSessionEdit && contentItem.isValid()) {
        if (!contentItem.bounds.isEmpty()) {
            const double fs = contentItem.fontSizePt > 0.0
                                  ? contentItem.fontSizePt : 12.0;
            const bool extractorUsable = block.pdfBounds.height() > 0.5
                                      && block.pdfBounds.height() <= fs * 2.5;
            if (!extractorUsable || contentItem.isFormField()) {
                m_activeEditBounds         = contentItem.bounds;
                m_activeEditOriginalBounds = contentItem.bounds;
                clampToPdfPage(block.page, m_activeEditBounds);
            }
        }
        if (contentItem.type == ContentItem::Type::Paragraph) {
            // The extractor block covers only the clicked line; fetch the full
            // multi-line paragraph (text + glyph-accurate bounds) so it is
            // edited as one unit without overpainting its surroundings.
            paragraphBounds = true;
            TextBlock para;
#ifdef HAVE_QT_PDF
            para = m_extractor->blockInRect(block.page, contentItem.bounds,
                                            erasedZones);
#elif defined(HAVE_POPPLER)
            if (m_popplerDoc)
                para = popplerBlockInRect(m_popplerDoc.get(), block.page,
                                          contentItem.bounds, erasedZones);
#endif
            // Sanity: a "paragraph" spanning most of the page is a detection
            // failure — editing it would open a viewport-sized frame. Fall
            // back to the clicked line in that case.
            const bool paraSane = para.isValid()
                               && para.pdfBounds.height() < pageSize.height() * 0.5;
            if (paraSane && !para.text.isEmpty()) {
                displayText                = para.text;
                m_activeEditBounds         = para.pdfBounds;
                m_activeEditOriginalBounds = para.pdfBounds;
                clampToPdfPage(block.page, m_activeEditBounds);
            } else if (!para.isValid() && !contentItem.text.isEmpty()) {
                displayText = contentItem.text;
            } else {
                paragraphBounds = false;   // keep the tight line bounds
            }
        }
        if (contentItem.isFormField()) {
            m_activeEditFieldName = contentItem.fieldName;
            displayText           = contentItem.text;
            // The widget appearance renders the value — erase it in the live
            // view only when there is an existing value to hide.
            m_activeEditNeedsBlank = !contentItem.text.isEmpty();
        }
    }
    m_activeEditOriginalText = displayText;
    if (isSessionEdit)
        m_activeEditFieldName = sessionEdit.formField;

    // Font size: stored session value > region-model detection > line-height
    // estimate from the block bounds.
    if (isSessionEdit && sessionEdit.fontSizePt > 0.0) {
        m_currentEditorFontSizePt = qMax(4, int(sessionEdit.fontSizePt));
    } else {
        const double detectedPt = (contentItem.isValid() && contentItem.fontSizePt > 0.0)
                                      ? contentItem.fontSizePt : 0.0;
        if (detectedPt > 0.0 && detectedPt <= 144.0) {
            const double polyEst   = block.pdfBounds.height() / 0.72;
            const bool   plausible = (detectedPt <= polyEst * 4.0)
                                   || (block.pdfBounds.height() >= 20.0);
            if (plausible) {
                m_currentEditorFontSizePt = qMax(4, int(detectedPt));
            } else {
                m_currentEditorFontSizePt = qMax(4, qRound(qMin(polyEst, 28.0)));
            }
        } else {
            const int    lineCount = qMax(1, displayText.count(u'\n') + 1);
            const double lineH     = qMin(m_activeEditBounds.height() / lineCount, 20.0);
            m_currentEditorFontSizePt = qMax(4, qRound(lineH / 0.72));
        }
    }
    // Sanity: the clicked line's glyph height caps the font size — a
    // detection outlier must not commit 2-3x oversized text.
    if (!isSessionEdit && !contentItem.isFormField()) {
        const double lineGlyphH = block.pdfBounds.height();
        if (lineGlyphH > 4.0 && lineGlyphH < 60.0
                && m_currentEditorFontSizePt > lineGlyphH * 1.5)
            m_currentEditorFontSizePt = qMax(4, qRound(lineGlyphH * 1.05));
    }
    Q_EMIT editorFontSizeChanged(m_currentEditorFontSizePt);

    // Font family/style: stored session value > region-model detection.
    if (isSessionEdit) {
        m_currentEditorFontFamily = sessionEdit.fontFamily;
        m_currentEditorBold       = sessionEdit.bold;
        m_currentEditorItalic     = sessionEdit.italic;
        m_editorFontChangedByUser = sessionEdit.fontChanged;
    } else if (!contentItem.fontFamily.isEmpty()) {
        m_currentEditorFontFamily = contentItem.fontFamily;
        m_currentEditorBold       = contentItem.bold;
        m_currentEditorItalic     = contentItem.italic;
        m_editorFontChangedByUser = false;
    } else {
        m_currentEditorFontFamily.clear();
        m_currentEditorBold       = false;
        m_currentEditorItalic     = false;
        m_editorFontChangedByUser = false;
    }
    Q_EMIT editorFontChanged(m_currentEditorFontFamily.isEmpty()
                                 ? QStringLiteral("Helvetica")
                                 : m_currentEditorFontFamily,
                             m_currentEditorBold, m_currentEditorItalic);

    // Expand edit bounds to cover the full rendered line height.  Glyph
    // polygons capture only the inked area (cap height ≈ 72% of font size);
    // large headings can leave original characters peeking out beneath the
    // blank fill without this.
    if (m_currentEditorFontSizePt > 0) {
        const double minH = m_currentEditorFontSizePt * 1.15;
        if (m_activeEditBounds.height() < minH) {
            m_activeEditBounds.setHeight(minH);
            clampToPdfPage(m_activeEditPage, m_activeEditBounds);
        }
        // Hard cap: no single-line edit frame should exceed 5× the detected
        // font size — catches polygon bounds that span whole table blocks.
        // Multi-line paragraph bounds from the region model are trusted.
        if (!paragraphBounds) {
            const double capH = m_currentEditorFontSizePt * 5.0;
            if (m_activeEditBounds.height() > capH) {
                m_activeEditBounds.setHeight(capH);
                clampToPdfPage(m_activeEditPage, m_activeEditBounds);
            }
        }
        m_activeEditOriginalBounds = m_activeEditBounds;
    }

    // Page rendered at 3 px per pt — shared by the color samplers below,
    // rendered lazily and at most once. Low-dpi renders consist mostly of
    // antialiasing pixels and make color detection unreliable.
    constexpr qreal kSampleScale = 3.0;
    QImage sampImg;
    const auto sampleImage = [&]() -> const QImage & {
        if (sampImg.isNull())
            sampImg = m_renderer->renderPage(block.page, kSampleScale);
        return sampImg;
    };

    // Text color: stored session color > exact content-stream fill color >
    // pixel sampling of the rendered page (last resort).
    if (isSessionEdit && sessionEdit.textColor.isValid()) {
        m_currentEditorColor = sessionEdit.textColor;
    } else if (!isSessionEdit && contentItem.isValid()
               && contentItem.textColor.isValid()) {
        m_currentEditorColor = contentItem.textColor;
    } else if (!sampleImage().isNull()) {
        const QRectF px(block.pdfBounds.topLeft() * kSampleScale,
                        block.pdfBounds.size() * kSampleScale);
        m_currentEditorColor = sampleTextColor(sampleImage(),
                                               px.toAlignedRect());
    }

    const int fontSize = qMax(6, qRound(m_currentEditorFontSizePt * scale));

    // Background color: stored session color > content-stream fill (light
    // fills only — dark detections are usually borders, not backgrounds) >
    // pixel ring around the bounds. The ring sees the ACTUAL surrounding
    // pixels, so it also covers backgrounds the stream scan can't reach
    // (form XObjects, shadings, images) and the Poppler backend, which has
    // no stream scan at all. Without it the blank fill paints white bars
    // over colored table rows.
    m_currentBgColor = Qt::white;
    if (isSessionEdit && sessionEdit.bgColor.isValid()) {
        m_currentBgColor = sessionEdit.bgColor;
    } else {
        QColor bg;
        if (contentItem.isValid() && contentItem.bgColor.isValid()) {
            const QColor c   = contentItem.bgColor;
            const qreal  lum = 0.299 * c.redF() + 0.587 * c.greenF()
                             + 0.114 * c.blueF();
            if (lum >= 0.70) bg = c;
        }
        if (!bg.isValid() && !sampleImage().isNull()) {
            const QRectF px(m_activeEditBounds.topLeft() * kSampleScale,
                            m_activeEditBounds.size() * kSampleScale);
            bg = sampleBackgroundColor(sampleImage(), px.toAlignedRect());
        }
        if (bg.isValid()) m_currentBgColor = bg;
    }

    // The editor font's metrics differ slightly from the PDF font. If the
    // detected bounds are even a few points too narrow for the editor font,
    // QTextEdit wraps the last word onto a bogus new line — and the commit
    // paints that wrap into the document. Widen the edit bounds so every
    // original line fits the editor font on ONE line (clamped to the page).
    if (m_currentEditorFontSizePt > 0 && !displayText.isEmpty()) {
        QFont measure(m_currentEditorFontFamily.isEmpty()
                          ? QStringLiteral("Helvetica")
                          : m_currentEditorFontFamily);
        measure.setStyleHint(QFont::SansSerif);
        measure.setPixelSize(m_currentEditorFontSizePt);   // 1 px == 1 pt here
        measure.setBold(m_currentEditorBold);
        measure.setItalic(m_currentEditorItalic);
        const QFontMetricsF fm(measure);
        qreal needW = 0.0;
        const QStringList lines = displayText.split(u'\n');
        for (const QString &ln : lines)
            needW = qMax(needW, fm.horizontalAdvance(ln));
        needW += m_currentEditorFontSizePt * 0.6;   // caret/AA headroom
        if (needW > m_activeEditBounds.width())
            m_activeEditBounds.setWidth(
                qMin(needW, pageSize.width() - m_activeEditBounds.left() - 2.0));
    }

    hideHoverHighlight();

    // Erasure targets: ONLY the tight glyph rects of the original text.
    // A whole-bounds erase would destroy graphics (chart bars, images,
    // rules) sharing the rectangle with the text.
    m_activeEditEraseRects.clear();
    if (m_activeEditNeedsBlank) {
#ifdef HAVE_QT_PDF
        m_activeEditEraseRects = m_extractor->glyphRects(
            block.page, m_activeEditOriginalBounds, erasedZones);
#elif defined(HAVE_POPPLER)
        if (m_popplerDoc)
            m_activeEditEraseRects = popplerGlyphRects(
                m_popplerDoc.get(), block.page, m_activeEditOriginalBounds,
                erasedZones);
#endif
    }

    // Recompute canvas bounds from the (possibly expanded) m_activeEditBounds so
    // both the blank fill rect and the editor frame cover the full rendered text.
    const QRectF canvasBounds(
        m_activeEditBounds.topLeft() * scale + QPointF(pageLbl->pos()),
        m_activeEditBounds.size() * scale);

    m_editorFrame->setDecorations(true);
    m_editorFrame->setForbiddenZones({});
    m_editorFrame->setPageRect(pageLbl->geometry());
    // Single-line edits extend horizontally while typing instead of wrapping.
    m_editorFrame->setGrowHorizontal(!displayText.contains(u'\n'));
    m_editorFrame->resetCommitGuard();
    m_undoSnapBefore = m_session->snapshotEdits();
    m_session->suspendEditsAt(block.page, block.pdfBounds);
    if (m_activeEditNeedsBlank)
        rerenderPageWithBlank(m_activeEditPage, m_activeEditBounds);
    else
        rerenderPage(m_activeEditPage);
    m_editorFrame->present(displayText, canvasBounds, fontSize, m_currentEditorColor,
                           m_currentEditorFontFamily,
                           m_currentEditorBold, m_currentEditorItalic);
#else
    Q_UNUSED(canvasPos)
#endif
}

#ifdef HAVE_PDF_RENDERING
EditSession::Edit DocumentView::makeSessionEdit(int page, const QRectF &bounds,
                                                const QRectF &sourceRect,
                                                const QString &text) const
{
    EditSession::Edit e;
    e.page        = page;
    e.pdfBounds   = bounds;
    e.sourceRect  = sourceRect;
    e.newText     = text;                       // null → blank (erase-only) edit
    e.fontSizePt  = m_currentEditorFontSizePt;
    e.textColor   = m_currentEditorColor;
    e.bgColor     = m_currentBgColor;
    e.fontFamily  = m_currentEditorFontFamily;
    e.bold        = m_currentEditorBold;
    e.italic      = m_currentEditorItalic;
    e.fontChanged = m_editorFontChangedByUser;
    e.formField   = m_activeEditFieldName;
    return e;
}
#endif

void DocumentView::commitCurrentEdit(const QString &newText)
{
#ifdef HAVE_PDF_RENDERING
    if (m_activeEditPage < 0) return;

    // Capture all state before hide() — hide() can trigger a recursive
    // focusOut→committed→commitCurrentEdit call, which exits early because
    // m_activeEditPage is already -1.
    const int    page       = m_activeEditPage;         // where the box is NOW
    const int    srcPage    = m_activeEditSourcePage >= 0 ? m_activeEditSourcePage
                                                          : m_activeEditPage;
    const QRectF bounds     = m_activeEditBounds;
    const QRectF origBounds = m_activeEditOriginalBounds;
    m_activeEditPage       = -1;
    m_activeEditSourcePage = -1;

    m_editorFrame->hide();  // may trigger recursive commit, which exits early ↑

    const QString trimNew = newText.trimmed();

    // m_undoSnapBefore was captured in handleEditClick/createTextFrame — BEFORE
    // suspendEditsAt() and before any live edits — so it is the true pre-edit state.
    const auto snapBefore = m_undoSnapBefore;

    // Every commit creates a tracked session entry so that overlapping blocks
    // can each be addressed independently in the same session.
    // Suspended edits are discarded — we replace them with the new content.
    m_session->clearSuspended();
    // Remove the live edit and any stale edit at origBounds using EXACT match.
    // removeAllAt (intersects) is intentionally avoided here: a full paragraph's
    // pdfBounds can span several hundred pixels, so it would accidentally delete
    // adjacent session edits that are close but distinct.
    m_session->removeEdit(srcPage, origBounds);
    if (page != srcPage || !bounds.intersects(origBounds))
        m_session->removeEdit(page, bounds);

    // Blank MUST be inserted before the text so applyToImage erases origBounds
    // before drawing the new text.  It is needed when:
    //   • editing existing text (handleEditClick) — erase original PDF or session text
    //   • text was moved to a new position — erase original location
    //   • text was deleted entirely
    // It is NOT created when the editor was opened fresh via drag (createTextFrame)
    // because that is a transparent overlay: new text drawn on top without erasing.
    // The second condition must be gated on m_activeEditNeedsBlank — for fresh drag
    // boxes the origBounds is just the initial drag rect, not real PDF content to erase.
    const bool needBlank = m_activeEditNeedsBlank;
    if (needBlank) {
        EditSession::Edit blank = makeSessionEdit(srcPage, origBounds, origBounds,
                                                  QString());
        blank.eraseRects = m_activeEditEraseRects;   // glyphs only, not the rect
        m_session->addEdit(std::move(blank));
    }

    if (!trimNew.isEmpty())
        m_session->addEdit(makeSessionEdit(page, bounds, origBounds, newText));
    m_activeEditFieldName.clear();

    // Snapshot AFTER state.  Only push an undo command if the session actually
    // changed — avoid polluting the stack with no-op "clicked and walked away" entries.
    const auto snapAfter = m_session->snapshotEdits();
    if (snapAfter != snapBefore)
        m_undoStack->push(new EditUndoCmd(m_session, this, srcPage, page,
                                          snapBefore, snapAfter));

    // Remember the original PDF block bounds so that the mouseRelease handler
    // for THIS SAME CLICK can avoid re-opening an editor for the block we just
    // committed — doing so would call rerenderPageWithBlank and blank the text.
    m_lastCommittedPage       = srcPage;
    m_lastCommittedOrigBounds = origBounds;

    // When blanking is needed, rerenderPageWithBlank ensures origBounds is
    // cleared via BOTH applyToImage (session blank edit) and the explicit fill —
    // belt-and-suspenders so the original text definitely disappears.
    if (needBlank)
        rerenderPageWithBlank(srcPage, origBounds);
    else
        rerenderPage(srcPage);
    if (page != srcPage)
        rerenderPage(page);   // box was dragged onto another page — paint it there
#else
    Q_UNUSED(newText)
#endif
}

void DocumentView::cancelCurrentEdit()
{
#ifdef HAVE_PDF_RENDERING
    const int page    = m_activeEditPage;
    const int srcPage = m_activeEditSourcePage;
    m_activeEditPage       = -1;
    m_activeEditSourcePage = -1;
    m_session->restoreSuspended();
    m_editorFrame->hide();
    if (page >= 0)
        rerenderPage(page);
    if (srcPage >= 0 && srcPage != page)
        rerenderPage(srcPage);
#else
    m_activeEditPage = -1;
#endif
}

// ── Drag-to-create text frame ─────────────────────────────────────────────────

void DocumentView::createTextFrame(const QRect &viewportDragRect)
{
#ifdef HAVE_PDF_RENDERING
    const QPoint scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
    const QRect canvasRect = viewportDragRect.translated(scroll);

    auto [pageIdx, pageLbl] = pageAtCanvasPos(canvasRect.center());
    if (pageIdx < 0) return;

    const qreal scale = PdfRenderer::screenScale(m_zoom);
    m_activeEditPage       = pageIdx;
    m_activeEditSourcePage = pageIdx;
    m_activeEditBounds = QRectF(
        (QPointF(canvasRect.topLeft()) - QPointF(pageLbl->pos())) / scale,
        QSizeF(canvasRect.size()) / scale);
    clampToPdfPage(pageIdx, m_activeEditBounds);
    m_activeEditOriginalBounds = m_activeEditBounds;
    m_activeEditOriginalText  = QString();
    m_activeEditNeedsBlank    = false;   // new text box overlay — don't erase background
    m_activeEditEraseRects.clear();
    m_activeEditFieldName.clear();
    m_undoSnapBefore          = m_session->snapshotEdits();  // before any live edits

    m_currentEditorColor = QColor(0x11, 0x11, 0x11);
    m_currentBgColor     = Qt::white;  // drag-created box: transparent overlay, no erasure
    // Fresh text box: default font, nothing detected to inherit.
    m_currentEditorFontFamily.clear();
    m_currentEditorBold       = false;
    m_currentEditorItalic     = false;
    m_editorFontChangedByUser = false;
    m_currentEditorFontSizePt = 12;
    Q_EMIT editorFontSizeChanged(m_currentEditorFontSizePt);
    Q_EMIT editorFontChanged(QStringLiteral("Helvetica"), false, false);

    hideHoverHighlight();
    const int fontSize = qMax(8, qRound(12.0 * scale));
    m_editorFrame->setDecorations(true);  // new text box: show border + handles
    m_editorFrame->setGrowHorizontal(false);   // user chose this width
    m_editorFrame->setPageRect(pageLbl->geometry());
    m_editorFrame->setForbiddenZones({});
    m_editorFrame->resetCommitGuard();
    m_editorFrame->present(QString(), QRectF(canvasRect), fontSize, m_currentEditorColor);
#else
    Q_UNUSED(viewportDragRect)
#endif
}

// ── Event filter ──────────────────────────────────────────────────────────────

bool DocumentView::eventFilter(QObject *obj, QEvent *e)
{
    // Page labels have WA_TransparentForMouseEvents so all clicks fall through to
    // m_canvas (their parent). We also handle viewport() for clicks in the margins.
    const bool fromCanvas   = (obj == m_canvas);
    const bool fromViewport = (obj == viewport());
    if (!fromCanvas && !fromViewport)
        return QScrollArea::eventFilter(obj, e);

    // Helpers: m_canvas coords are the canonical space; rubber band lives in viewport.
    const QPoint scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
    auto toCanvas   = [&](const QPoint &p) { return fromCanvas ? p : p + scroll; };
    auto toViewport = [&](const QPoint &p) { return fromCanvas ? p - scroll : p; };

    if (e->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(e);
        if (me->button() == Qt::LeftButton) {
            const QPoint cvsPos = toCanvas(me->pos());
            const QPoint vpPos  = toViewport(me->pos());

            if (m_editMode && m_tool == Tool::Text) {
#ifdef HAVE_PDF_RENDERING
                // Let TextBoxFrame/InlineEditor handle clicks inside the active frame.
                if (m_editorFrame->isVisible() && m_editorFrame->geometry().contains(cvsPos))
                    return QScrollArea::eventFilter(obj, e);
                // Commit the active edit before starting a new one.
                // cancelCurrentEdit must NOT be used here — the user's typed
                // text must be kept, not discarded.
                commitCurrentEdit(m_editorFrame->currentText());
#endif
                m_textDragStart = cvsPos;   // stored in canvas coords
                m_textTracking  = true;
                m_textDragging  = false;
                qWarning() << "[EF] text press cvsPos=" << cvsPos << "editMode=" << m_editMode;
                return true;
            }

            if (m_tool == Tool::Image) {
                // Click on a detected image frame extracts and places it;
                // otherwise start the rubber band, but only on a page.
                if (m_imageLayer->takeDetectedRegionAt(cvsPos)) return true;
                m_imageLayer->handlePress(cvsPos);   // false → outside page, swallowed
                return true;
            }

            switch (m_tool) {
            case Tool::Select:
                m_selection->handlePress(cvsPos);
                break;
            case Tool::Pan:
                m_panStart        = vpPos;
                m_panScrollOrigin = { horizontalScrollBar()->value(),
                                     verticalScrollBar()->value() };
                viewport()->setCursor(Qt::ClosedHandCursor);
                break;
            default: break;
            }
        }
    } else if (e->type() == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(e);
        // Hover feedback over detected content regions (Acrobat-style).
        if (m_editMode && m_tool == Tool::Text && !m_textTracking)
            updateHoverHighlight(toCanvas(me->pos()));
        else
            hideHoverHighlight();
        if (m_editMode && m_tool == Tool::Text && m_textTracking) {
            const QPoint cvsPos = toCanvas(me->pos());
            if (!m_textDragging && (cvsPos - m_textDragStart).manhattanLength() > 6)
                m_textDragging = true;
            if (m_textDragging) {
                m_rubberBand->setGeometry(
                    QRect(toViewport(m_textDragStart), toViewport(cvsPos)).normalized());
                m_rubberBand->show();
            }
            return true;
        }
        if (m_tool == Tool::Image && m_imageLayer->isDragTracking()) {
            bool dragging = false;
            const QPoint cvsPos = m_imageLayer->handleMove(toCanvas(me->pos()), &dragging);
            if (dragging) {
                m_rubberBand->setGeometry(
                    QRect(toViewport(m_imageLayer->dragStart()),
                          toViewport(cvsPos)).normalized());
                m_rubberBand->show();
            }
            return true;
        }
        // Select/Pan are navigation tools — they work in edit mode as well,
        // matching the press handler above which is not gated on m_editMode.
        {
            const QPoint vpPos = toViewport(me->pos());
            switch (m_tool) {
            case Tool::Select:
                if ((me->buttons() & Qt::LeftButton)
                        && m_selection->handleMove(toCanvas(me->pos())))
                    return true;
                break;
            case Tool::Pan:
                if (me->buttons() & Qt::LeftButton) {
                    const QPoint d = vpPos - m_panStart;
                    horizontalScrollBar()->setValue(m_panScrollOrigin.x() - d.x());
                    verticalScrollBar()->setValue(m_panScrollOrigin.y() - d.y());
                    return true;
                }
                break;
            default: break;
            }
        }
    } else if (e->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(e);
        if (me->button() == Qt::LeftButton) {
            if (m_editMode && m_tool == Tool::Text && m_textTracking) {
                m_textTracking = false;
                if (m_textDragging) {
                    m_textDragging = false;
                    const QRect band = m_rubberBand->geometry();   // viewport coords
                    m_rubberBand->hide();
                    if (band.width() > 30 && band.height() > 15)
                        createTextFrame(band);                       // expects viewport rect ✓
                } else {
                    handleEditClick(m_textDragStart);                // canvas coords ✓
                }
                return true;
            }
            if (m_tool == Tool::Image && m_imageLayer->isDragTracking()) {
                if (m_imageLayer->handleRelease()) {
                    const QRect band = m_rubberBand->geometry();  // viewport coords
                    m_rubberBand->hide();
                    if (band.width() > 20 && band.height() > 20) {
                        const QString path = QFileDialog::getOpenFileName(this,
                            tr("Bild einfügen"), {},
                            tr("Bilder (*.png *.jpg *.jpeg *.bmp *.gif *.tiff *.webp);;Alle Dateien (*)"));
                        if (!path.isEmpty()) {
                            const QImage img(path);
                            if (!img.isNull())
                                m_imageLayer->placeInRect(img, band.translated(scroll));
                        }
                    }
                }
                return true;
            }
            switch (m_tool) {
            case Tool::Select:
                m_rubberBand->hide();
                if (m_selection->handleRelease()) return true;
                break;
            case Tool::Pan:    viewport()->setCursor(Qt::OpenHandCursor);   break;
            default: break;
            }
        }
    } else if (e->type() == QEvent::ContextMenu) {
        // Show the general context menu whenever edit mode is active or a
        // content-editing tool is selected.  For the image tool, ImageAnnotation
        // accepts its own context-menu event so it never reaches the canvas — the
        // general menu appears only when clicking on empty canvas area.
        // Marked page text offers "Kopieren" regardless of the mode.
        if (m_editMode || m_tool == Tool::Text || m_tool == Tool::Image
                || m_selection->hasSelection()) {
            auto *ce = static_cast<QContextMenuEvent *>(e);
            showGeneralContextMenu(ce->globalPos());
            return true;
        }
    }

    return QScrollArea::eventFilter(obj, e);
}

// ── Drag & Drop ───────────────────────────────────────────────────────────────

static bool isImagePath(const QString &p)
{
    static const QStringList exts = {
        ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tiff", ".tif", ".webp"
    };
    for (const QString &ext : exts)
        if (p.endsWith(ext, Qt::CaseInsensitive)) return true;
    return false;
}

void DocumentView::dragEnterEvent(QDragEnterEvent *e)
{
    if (!e->mimeData()->hasUrls()) { e->ignore(); return; }
    for (const QUrl &url : e->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive) ||
            (m_tool == Tool::Image && isImagePath(path))) {
            e->acceptProposedAction();
            return;
        }
    }
    e->ignore();
}

void DocumentView::dragMoveEvent(QDragMoveEvent *e) { e->acceptProposedAction(); }

void DocumentView::dropEvent(QDropEvent *e)
{
    for (const QUrl &url : e->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
            openFile(path);
            e->acceptProposedAction();
            return;
        }
        if (m_tool == Tool::Image && isImagePath(path)) {
            const QImage img(path);
            if (!img.isNull()) {
                const QPoint vpPos  = e->position().toPoint();
                const QPoint scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
                m_imageLayer->place(img, vpPos + scroll);
                e->acceptProposedAction();
                return;
            }
        }
    }
}
