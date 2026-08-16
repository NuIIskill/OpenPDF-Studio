// Part of DocumentView — see DocumentView.hpp. Split across translation
// units purely for readability; one 2500-line file was not reviewable.
// Inline text editing: opening an edit, committing it, the editor's font state.

#include "ui/DocumentView.hpp"

#include "app/PdfPwStore.hpp"
#include "engine/document/DocumentSource.hpp"
#include "engine/edit/InkMetrics.hpp"
#include "app/SafeWrite.hpp"
#include "app/SessionStore.hpp"
#include "ui/tools/ImageAnnotation.hpp"
#include "ui/view/ImageAnnotationLayer.hpp"
#include "ui/view/HoverHighlight.hpp"
#include "ui/view/PageLayoutEngine.hpp"
#include "ui/view/ZoomController.hpp"
#include "ui/view/TextSelectionController.hpp"
#include "ui/dialogs/PasswordDialog.hpp"

#include <QFileInfo>

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
    , m_src(std::make_unique<DocumentSource>())
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
            this, [this]() { reportCurrentPage(); syncVisibleRect(); });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this]() { syncVisibleRect(); });

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
    connect(m_imageLayer, &ImageAnnotationLayer::imageAdded, this, [this](int page) {
        m_journal.recordChange({ DocumentHistory::Kind::ImageInserted, page });
    });
    connect(m_imageLayer, &ImageAnnotationLayer::imageRemoved, this, [this](int page) {
        m_journal.recordChange({ DocumentHistory::Kind::ImageRemoved, page });
    });

    // Undo and redo move the document between states the history already
    // knows, so the history follows the stack instead of recording anything.
    connect(m_undoStack, &QUndoStack::indexChanged, this, [this](int index) {
        if (m_journal.restoring || m_edit.pushingEdit || m_journal.history()->isEmpty()) return;
        m_journal.history()->setCurrentIndex(m_journal.history()->indexForUndoIndex(index));
    });

    m_layoutEngine = new PageLayoutEngine(m_canvas, m_layout, m_gridCanvas, this);

    m_zoomCtl = new ZoomController(this, this, m_layout, m_layoutEngine, this);
    connect(m_zoomCtl, &ZoomController::zoomChanged,
            this, &DocumentView::zoomChanged);
    connect(m_zoomCtl, &ZoomController::viewportChanged,
            this, &DocumentView::syncVisibleRect);
    connect(m_zoomCtl, &ZoomController::zoomApplied,
            this, &DocumentView::repositionForZoom);

    m_hover = new HoverHighlight(this, this);

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
        if (m_edit.activeEditSourcePage >= 0 && m_edit.activeEditNeedsBlank)
            rerenderPageWithBlank(m_edit.activeEditSourcePage, m_edit.activeEditOriginalBounds);
    });
    connect(m_editorFrame, &TextBoxFrame::boundsChanged, this, [this](const QRectF &inner) {
        if (m_edit.activeEditPage < 0) return;
        // The box is freely draggable across pages: the page under its center
        // owns it. Between pages (margins/gaps) keep the last owner.
        auto [pg, lbl] = pageAtCanvasPos(inner.center().toPoint());
        if (pg < 0 || !lbl) {
            pg  = m_edit.activeEditPage;
            lbl = pageLabel(pg);
            if (!lbl) return;
        }
        if (pg != m_edit.activeEditPage) {
            m_edit.activeEditPage = pg;
            // Growth and resize clamps must use the page the box is on now.
            m_editorFrame->setPageRect(lbl->geometry());
        }
        const qreal scale = PdfRenderer::screenScale(m_zoomCtl->zoom());
        QRectF newBounds(
            (inner.topLeft() - QPointF(lbl->pos())) / scale,
            inner.size() / scale);
        m_edit.clampToPdfPage(pg, newBounds);
        m_edit.activeEditBounds = newBounds;
    });
    m_hover->setEditorFrame(m_editorFrame);

#  ifdef HAVE_QT_PDF
    m_selection->setSource(m_src->renderer(), m_src->document());
    m_layoutEngine->setSource(m_src->renderer(), m_session);
#  endif
#endif
    // HAVE_POPPLER: m_src->renderer() and m_src->popplerDoc() are created per-file in openFile()

    m_ocrEngine = new OcrEngine();
#ifdef HAVE_PDF_RENDERING
    // After m_ocrEngine exists. On the Poppler path m_src->renderer() is still null
    // here — openFile() hands the real one over per document.
    m_imageLayer->setSource(m_src->renderer(), m_session, m_ocrEngine, m_src->contentPath());
#endif

    // Last, so every collaborator it is handed already exists — m_ocrEngine in
    // particular is built further up this constructor.
    m_edit.attach(this, m_src.get(), m_editorFrame, m_zoomCtl, m_hover,
                  m_ocrEngine, m_undoStack);

    // The journal records image state with every change, but the images live
    // in a widget layer it cannot see — so it asks for them.
    m_journal.attach(m_src.get(), m_undoStack, [this] { return imageStates(); });
#ifdef HAVE_PDF_RENDERING
    m_journal.setSession(m_session);
#endif
#ifdef HAVE_PDF_RENDERING
    m_edit.setSession(m_session);
#endif
    connect(&m_edit, &EditController::fontSizeChanged,
            this, &DocumentView::editorFontSizeChanged);
    connect(&m_edit, &EditController::fontChanged,
            this, &DocumentView::editorFontChanged);
    connect(&m_edit, &EditController::pageNeedsRerender,
            this, &DocumentView::rerenderPage);
    connect(&m_edit, &EditController::pageNeedsBlank,
            this, &DocumentView::rerenderPageWithBlank);
    // recordChange takes an optional snapshot source the controller has no
    // business knowing about; the default is what a text edit wants.
    connect(&m_edit, &EditController::changeRecorded, this,
            [this](const DocumentHistory::Change &c) { m_journal.recordChange(c); });
}

DocumentView::~DocumentView()
{
    // A view can go away without clearDocument() — closing a tab deletes it,
    // and quitting takes every view down with the window. Its session working
    // copy has to go with it either way, or one file is left behind per saved
    // document. discard() only touches files inside the session directory, so
    // handing it a user's own document does nothing. A view that dies without
    // running this (a crash) leaves the copy for recovery, which is the point.
    SessionStore::discard(m_src->contentPath());
    delete m_ocrEngine;
#ifdef HAVE_PDF_RENDERING
    // The provider references the backend document (raw pointer on Poppler) —
    // destroy it before the document regardless of member declaration order.
    delete m_session;
#endif
}

// ── File ──────────────────────────────────────────────────────────────────────

void DocumentView::setEditMode(bool on)
{
    if (m_editMode == on) return;
#ifdef HAVE_PDF_RENDERING
    commitCurrentEdit(m_editorFrame->currentText());
#endif
    m_hover->hide();
    m_editMode = on;
    if (!on) setTool(m_tool); // restore tool cursor when leaving edit mode
}

void DocumentView::rerenderPageWithBlank(int page, const QRectF &pdfBoundsPts)
{
#ifdef HAVE_PDF_RENDERING
    m_layoutEngine->rerenderPageWithBlank(page, pdfBoundsPts, m_edit.activeEditEraseRects);
#else
    Q_UNUSED(page) Q_UNUSED(pdfBoundsPts)
#endif
}

// ── Grid view ─────────────────────────────────────────────────────────────────

void DocumentView::handleEditClick(const QPoint &canvasPos)
{
#ifdef HAVE_PDF_RENDERING
    m_edit.handleClick(canvasPos);
#else
    Q_UNUSED(canvasPos)
#endif
}

#ifdef HAVE_PDF_RENDERING
void DocumentView::discardEditHistory()
{
    m_session->clearSuspended();
    m_session->clear();
    // The placed images ARE the session's image edits, drawn as widgets. Left
    // behind they would hang over a document that no longer contains them —
    // and the next save would paint them into it a second time.
    m_imageLayer->clear();
    m_undoStack->clear();
    m_journal.savedImageRevision = m_session->imageRevision();
}

#endif

void DocumentView::commitCurrentEdit(const QString &newText)
{
#ifdef HAVE_PDF_RENDERING
    m_edit.commit(newText);
#else
    Q_UNUSED(newText)
#endif
}

void DocumentView::cancelCurrentEdit()
{
#ifdef HAVE_PDF_RENDERING
    m_edit.cancel();
#endif
}

void DocumentView::createTextFrame(const QRect &viewportDragRect)
{
#ifdef HAVE_PDF_RENDERING
    m_edit.createTextFrame(viewportDragRect);
#else
    Q_UNUSED(viewportDragRect)
#endif
}

// ── FormatBar → editor (delegated to EditController) ─────────────────────────

void DocumentView::refreshEditorFontLive()                     { m_edit.refreshFontLive(); }
void DocumentView::setEditorFontFamily(const QString &family)  { m_edit.setFontFamily(family); }
void DocumentView::setEditorBold(bool on)                      { m_edit.setBold(on); }
void DocumentView::setEditorItalic(bool on)                    { m_edit.setItalic(on); }
void DocumentView::setEditorFontSize(int ptSize)               { m_edit.setFontSize(ptSize); }
void DocumentView::setEditorTextColor(const QColor &color)     { m_edit.setTextColor(color); }
