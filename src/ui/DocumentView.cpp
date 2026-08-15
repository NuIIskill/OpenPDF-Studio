#include "DocumentView.hpp"

#include "app/PdfPwStore.hpp"
#include "app/SafeWrite.hpp"
#include "app/SessionStore.hpp"
#include "tools/ImageAnnotation.hpp"
#include "view/ImageAnnotationLayer.hpp"
#include "view/PageLayoutEngine.hpp"
#include "view/TextSelectionController.hpp"
#include "dialogs/PasswordDialog.hpp"

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
    , m_history(new DocumentHistory(this))
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
        recordChange({ DocumentHistory::Kind::ImageInserted, page });
    });
    connect(m_imageLayer, &ImageAnnotationLayer::imageRemoved, this, [this](int page) {
        recordChange({ DocumentHistory::Kind::ImageRemoved, page });
    });

    // Undo and redo move the document between states the history already
    // knows, so the history follows the stack instead of recording anything.
    connect(m_undoStack, &QUndoStack::indexChanged, this, [this](int index) {
        if (m_restoring || m_pushingEdit || m_history->isEmpty()) return;
        m_history->setCurrentIndex(m_history->indexForUndoIndex(index));
    });

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
    // A view can go away without clearDocument() — closing a tab deletes it,
    // and quitting takes every view down with the window. Its session working
    // copy has to go with it either way, or one file is left behind per saved
    // document. discard() only touches files inside the session directory, so
    // handing it a user's own document does nothing. A view that dies without
    // running this (a crash) leaves the copy for recovery, which is the point.
    SessionStore::discard(m_filePath);
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
    discardEditHistory();
    m_document->close();
    m_ocrCache.clear();
#elif defined(HAVE_POPPLER)
    discardEditHistory();
    m_ocrCache.clear();
    setPopplerSource(nullptr);   // closes the document and frees its handles
#endif
    SessionStore::discard(m_filePath);
    m_filePath.clear();
    m_targetPath.clear();
    m_workingCopyDirty = false;
    // The log describes a document that is no longer open; its snapshots go
    // with it, or the session directory fills up with states nothing points at.
    m_history->reset();
#ifdef HAVE_PDF_RENDERING
    // m_renderer is already gone on the Poppler path — drop the stale handles.
    m_imageLayer->setSource(m_renderer, m_session, m_ocrEngine, QString());
#endif
    m_pageCount = 0;
    m_lastReportedPage = -1;

    m_layoutEngine->clearPages();

    m_imageLayer->clear();

    m_dropHint->show();
}

bool DocumentView::openFile(const QString &path)
{
    if (path.isEmpty()) return false;
    cancelCurrentEdit();

    // Opening a document ends any working-copy state of the previous one. The
    // old working file is only dropped once the new document actually loaded.
    const QString previousWorkingFile =
        (path != m_filePath) ? m_filePath : QString();

    if (m_viewMode == ViewMode::Grid)
        setViewMode(ViewMode::Single);

#ifdef HAVE_QT_PDF
    // Load before dropping any state: a failed load used to leave the session
    // cleared and the document closed while the page widgets and page count
    // still described the previous file — the view went blank with no error.
    // QPdfDocument requires an explicit close before loading another file.
    m_document->close();
    // A password already known for this file (reopen, working copy) is tried
    // before the user is asked again.
    m_document->setPassword(PdfPwStore::get(path));
    auto err = m_document->load(path);

    // Encrypted documents used to fail here with nothing but a log line: the
    // view went blank and never said why. Ask, and keep asking until it opens
    // or the user gives up.
    for (int attempt = 0; err == QPdfDocument::Error::IncorrectPassword; ++attempt) {
        PasswordDialog prompt(QFileInfo(path).fileName(), attempt > 0, this);
        if (prompt.exec() != QDialog::Accepted) break;      // cancelled
        const QString entered = prompt.password();

        m_document->close();
        m_document->setPassword(entered);
        err = m_document->load(path);
        if (err == QPdfDocument::Error::None) {
            // Every other reader of this file — content scanner, edit session,
            // exporter — picks the password up from here.
            PdfPwStore::set(path, entered);
        }
    }

    if (err != QPdfDocument::Error::None) {
        qWarning() << "DocumentView: could not open" << path << "-" << err;
        // Put the previous document back so the view keeps showing it.
        m_document->close();
        if (!m_filePath.isEmpty() && m_filePath != path) {
            m_document->setPassword(PdfPwStore::get(m_filePath));
            m_document->load(m_filePath);
        }
        return false;
    }
    discardEditHistory();
    m_filePath  = path;
    m_targetPath.clear();
    m_workingCopyDirty = false;
    SessionStore::discard(previousWorkingFile);
    m_pageCount = m_document->pageCount();
    m_imageLayer->setSource(m_renderer, m_session, m_ocrEngine, m_filePath);
    m_layoutEngine->setPageCount(m_pageCount);
    resetContentProvider();
    m_dropHint->hide();
    m_layoutEngine->buildPages();
    // Once the scroll area has laid the new pages out, hand the engine the
    // window it actually renders — until then the page positions are all 0.
    QMetaObject::invokeMethod(this, [this]() { syncVisibleRect(); },
                              Qt::QueuedConnection);
    noteDocumentOpened();
    Q_EMIT fileOpened(m_filePath, m_pageCount);
    m_lastReportedPage = 0;          // freshly opened documents start at the top
    Q_EMIT pageChanged(1, m_pageCount);
    return true;

#elif defined(HAVE_POPPLER)
    auto doc = Poppler::Document::load(path);

    // The Poppler backend needs the same password handling as the Qt one — it
    // is what the Windows build ships. Without this an encrypted document just
    // returned false here and the window stayed empty with no explanation.
    if (doc && doc->isLocked()) {
        const QByteArray known = PdfPwStore::get(path).toUtf8();
        if (!known.isEmpty()) doc->unlock(known, known);
    }
    for (int attempt = 0; doc && doc->isLocked(); ++attempt) {
        PasswordDialog prompt(QFileInfo(path).fileName(), attempt > 0, this);
        if (prompt.exec() != QDialog::Accepted) break;          // cancelled
        const QByteArray entered = prompt.password().toUtf8();
        doc->unlock(entered, entered);
        if (!doc->isLocked()) PdfPwStore::set(path, prompt.password());
    }

    if (!doc || doc->isLocked()) return false;
    doc->setRenderHint(Poppler::Document::Antialiasing);
    doc->setRenderHint(Poppler::Document::TextAntialiasing);
    discardEditHistory();
    m_ocrCache.clear();
    m_filePath   = path;
    m_targetPath.clear();
    m_workingCopyDirty = false;
    setPopplerSource(std::move(doc));
    // Only now that the reader let go of it may the working file be removed.
    SessionStore::discard(previousWorkingFile);
    m_pageCount  = m_popplerDoc->numPages();
    m_layoutEngine->setPageCount(m_pageCount);
    resetContentProvider();
    m_dropHint->hide();
    m_layoutEngine->buildPages();
    // See the Qt-PDF branch: the render window is only measurable once the
    // scroll area has laid the new pages out.
    QMetaObject::invokeMethod(this, [this]() { syncVisibleRect(); },
                              Qt::QueuedConnection);
    noteDocumentOpened();
    Q_EMIT fileOpened(m_filePath, m_pageCount);
    m_lastReportedPage = 0;          // freshly opened documents start at the top
    Q_EMIT pageChanged(1, m_pageCount);
    return true;

#else
    m_filePath  = path;
    m_targetPath.clear();
    m_workingCopyDirty = false;
    SessionStore::discard(previousWorkingFile);
    m_pageCount = 1;
    m_dropHint->show();
    retranslateUi();
    noteDocumentOpened();
    Q_EMIT fileOpened(m_filePath, m_pageCount);
    m_lastReportedPage = 0;          // freshly opened documents start at the top
    Q_EMIT pageChanged(1, m_pageCount);
    return true;
#endif
}

// ── Zoom / Tool / Edit mode ───────────────────────────────────────────────────

void DocumentView::setZoom(int percent)
{
    // Zoom from the toolbar, the menu or a shortcut: hold the middle of the
    // viewport. Without an anchor the canvas keeps its top-left corner and the
    // passage the user was reading slides out of view.
    applyZoom(percent, QPoint(viewport()->width() / 2, viewport()->height() / 2));
}

void DocumentView::applyZoom(int percent, const QPoint &viewportAnchor)
{
    if (m_zoom == percent || percent <= 0) return;
    const int   oldZoom  = m_zoom;
    const qreal oldScale = screenScale();

    // What sits under the anchor right now, in PDF points on a page — the one
    // thing that must not move. Canvas margins and page gaps do not scale with
    // the zoom, so scaling the scroll position alone drifts.
    const QPoint canvasAnchor = -m_canvas->pos() + viewportAnchor;
    auto [anchorPage, anchorLbl] = pageAtCanvasPos(canvasAnchor);
    QPointF anchorPt;
    if (anchorPage >= 0 && anchorLbl && oldScale > 0)
        anchorPt = (QPointF(canvasAnchor) - QPointF(anchorLbl->pos())) / oldScale;

    m_zoom = percent;
    m_layoutEngine->setZoom(percent);   // resizes the pages, renders them shortly after
    Q_EMIT zoomChanged(percent);
    updateScrollRange();

    const QLabel *anchorNow = anchorPage >= 0 ? pageLabel(anchorPage) : nullptr;
    int tx, ty;
    if (anchorNow) {
        const QPointF target = QPointF(anchorNow->pos()) + anchorPt * screenScale();
        tx = qRound(target.x()) - viewportAnchor.x();
        ty = qRound(target.y()) - viewportAnchor.y();
    } else {
        // The anchor sat in a margin or between two pages: no page point to
        // hold on to, so scale the scroll position instead.
        const qreal ratio = qreal(percent) / oldZoom;
        tx = qRound(ratio * (horizontalScrollBar()->value() + viewportAnchor.x()))
             - viewportAnchor.x();
        ty = qRound(ratio * (verticalScrollBar()->value() + viewportAnchor.y()))
             - viewportAnchor.y();
    }
    horizontalScrollBar()->setValue(tx);
    verticalScrollBar()->setValue(ty);
    // The scroll area can still be a layout pass behind, in which case the
    // values above were clamped to the old range. Retry once it has caught
    // up — the target is absolute, so this is idempotent.
    if (horizontalScrollBar()->value() != tx || verticalScrollBar()->value() != ty) {
        QTimer::singleShot(0, this, [this, tx, ty]() {
            horizontalScrollBar()->setValue(tx);
            verticalScrollBar()->setValue(ty);
            syncVisibleRect();
        });
    }
    syncVisibleRect();

    // Page labels can be re-laid out once more after this, so the highlights
    // are repositioned again when that layout has settled.
    m_selection->relayout();
    QTimer::singleShot(0, this, [this]() { m_selection->relayout(); });

#ifdef HAVE_PDF_RENDERING
    // The blank that hides the original text sticks to its page inside the
    // layout engine, so a re-render at the new zoom keeps it — only the editor
    // frame has to follow.
    if (m_activeEditPage >= 0 && m_editorFrame->isVisible()) {
        // Reposition the editor frame for the new zoom.  A 0 ms timer defers
        // the reposition until after the layout has settled — the frame may
        // still be growing to the text it holds.
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
                cb, qMax(6, qRound(m_currentEditorRenderSizePt * scale)));
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

        const int step = qMax(1, m_zoomStep);
        const int next = delta > 0 ? qMin(m_zoom + step, 300)
                                   : qMax(m_zoom - step, 25);

        // Anchor on the pointer position CARRIED BY THE EVENT. QCursor::pos()
        // has no meaning on Wayland (a client cannot query the pointer), so it
        // used to anchor the zoom at a stale point and threw the page around.
        const QPoint anchor =
            m_zoomToPointer
                ? viewport()->mapFromGlobal(e->globalPosition().toPoint())
                : QPoint(viewport()->width() / 2, viewport()->height() / 2);
        applyZoom(next, anchor);

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

bool DocumentView::openWorkingCopy(const QString &contentPath,
                                   const QString &targetPath,
                                   const DocumentHistory::Change &change)
{
    // openFile records the state; it needs to know whether this file is a new
    // document or the result of a change to the one already open.
    m_openChange = change;
    struct ClearOnReturn {
        DocumentHistory::Change *c;
        ~ClearOnReturn() { *c = DocumentHistory::Change{}; }
    } clearOnReturn { &m_openChange };

    // qpdf keeps a document's encryption when it rewrites it, so the working
    // copy of a protected file is protected too — with the same password. Carry
    // it across, or the user is asked again for a file they never chose.
    if (!targetPath.isEmpty() && !PdfPwStore::has(contentPath))
        PdfPwStore::set(contentPath, PdfPwStore::get(targetPath));

    if (!openFile(contentPath)) return false;
    if (targetPath.isEmpty() || targetPath == contentPath) return true;

    m_targetPath       = targetPath;
    m_workingCopyDirty = true;
    // openFile announced the working file — re-announce under the document's
    // own name so the title bar shows the PDF the user opened.
    Q_EMIT fileOpened(m_targetPath, m_pageCount);
    return true;
}

bool DocumentView::saveToFile(const QString &path)
{
#ifdef HAVE_PDF_RENDERING
    // From here on the document on disk may stop being the one the history's
    // first entry stands for, so that entry gets its own copy now.
    m_history->materializeSnapshot();
#endif
#ifdef HAVE_QT_PDF
    commitCurrentEdit(m_editorFrame->currentText());

    // Saving writes the session out; it does not end it. The document keeps
    // rendering from its own base with the edits layered on, so undo and redo
    // reach across the save for as long as the file stays open.
    const bool detached = detachSourceFrom(path);
    if (!m_session->saveToFile(path, m_document, m_pageCount, m_filePath))
        return false;

    if (detached) {
        markSaved(path);
        return true;
    }

    // The base could not be put out of harm's way, so it is gone: the file we
    // read from has just been overwritten with the edits baked in. Start over
    // from the result — and drop the history with it, or undo would paint
    // those same edits on top a second time (see discardEditHistory).
    discardEditHistory();
    const QString previousWorkingFile = (path != m_filePath) ? m_filePath : QString();
    m_filePath = path;
    m_targetPath.clear();
    m_workingCopyDirty = false;
    m_document->close();
    m_document->load(path);
    // Only now that the reader let go of it may the working file be removed.
    SessionStore::discard(previousWorkingFile);
    resetContentProvider();
    m_layoutEngine->rerenderAll();
    recordSavedOverBase(path);
    return true;
#elif defined(HAVE_POPPLER)
    commitCurrentEdit(m_editorFrame->currentText());
    if (!m_popplerDoc || !m_renderer) return false;

    // Saving writes the session out; it does not end it. See the Qt path above.
    const bool detached = detachSourceFrom(path);

    // The new document is written beside the target first, because the render
    // reads from the document that is still open — and that may be the very
    // file being saved to.
    const QString staging = SafeWrite::stagingPath(path);
    if (staging.isEmpty()) return false;
    if (!writePopplerRaster(staging)) { SafeWrite::discard(staging); return false; }

    // With the base moved aside, the target is not a file this process holds
    // open, so the swap needs none of the close-and-reopen dance below — the
    // one Windows forces when Poppler is still reading the file being replaced.
    if (detached) {
        if (!SafeWrite::commit(staging, path)) return false;
        markSaved(path);
        return true;
    }

    // Windows refuses to rename or delete a file that is still open: Poppler
    // holds the document it renders from without allowing deletion, so saving
    // over the open document failed at the swap below and the save silently did
    // nothing. Let go of the file first — it is reopened either way, from the
    // new file when the swap worked and from the old one when it did not.
    const QString previousWorkingFile = (path != m_filePath) ? m_filePath : QString();
    const QString reopenPath = m_filePath;
    setPopplerSource(nullptr);

    if (!SafeWrite::commit(staging, path)) {
        setPopplerSource(loadPopplerDocument(reopenPath));
        resetContentProvider();
        return false;
    }

    // Reload from the saved file so subsequent saves work on the updated
    // content. The edits are part of the file now, which is exactly why the
    // undo history goes with them — see discardEditHistory.
    discardEditHistory();
    auto reloaded = loadPopplerDocument(path);
    if (reloaded) {
        m_filePath = path;
        m_targetPath.clear();
        m_workingCopyDirty = false;
        setPopplerSource(std::move(reloaded));
        // Only now that the reader let go of it may the working file be removed.
        SessionStore::discard(previousWorkingFile);
    } else {
        // The file was written, but it cannot be read back. Keep showing the
        // document that was open instead of leaving the view blank — and keep
        // its working file, which is what that view is reading from.
        qWarning() << "[SAVE] wrote" << path << "but could not reopen it";
        setPopplerSource(loadPopplerDocument(reopenPath));
    }
    resetContentProvider();
    m_layoutEngine->rerenderAll();
    recordSavedOverBase(path);
    return true;
#else
    Q_UNUSED(path)
    return false;
#endif
}

#ifdef HAVE_PDF_RENDERING
void DocumentView::markSaved(const QString &path)
{
    // The session lives on, so "saved" is a mark on it rather than the absence
    // of edits: the undo stack's clean index follows undo AND redo, so stepping
    // back past the save reports unsaved changes again and stepping forward to
    // it reports none.
    m_undoStack->setClean();
    m_savedImageRevision = m_session->imageRevision();
    m_workingCopyDirty   = false;
    // m_filePath is the session's own base now; `path` is the document the user
    // thinks of as open, which is what the title bar has to show.
    m_targetPath = (QFileInfo(path).absoluteFilePath()
                        == QFileInfo(m_filePath).absoluteFilePath())
                       ? QString() : path;

    DocumentHistory::Change saved;
    saved.kind  = DocumentHistory::Kind::Saved;
    saved.count = m_pageCount;
    saved.text  = QFileInfo(path).fileName();
    recordChange(saved);

    Q_EMIT fileOpened(currentFile(), m_pageCount);
}
#endif

// ── Change history ────────────────────────────────────────────────────────────

QList<DocumentHistory::ImageState> DocumentView::imageStates() const
{
    QList<DocumentHistory::ImageState> out;
    const QList<ImageAnnotationLayer::Placed> placed = m_imageLayer->placedImages();
    out.reserve(placed.size());
    for (const auto &p : placed)
        out.append({ p.page, p.pdfBounds, p.image });
    return out;
}

void DocumentView::recordSavedOverBase(const QString &path)
{
    // This save could not keep the document it was layered on: the edits are
    // in the file now and the session started over. The written file is
    // therefore the state to come back to — the undo indices recorded before
    // it belong to a stack that no longer exists.
    DocumentHistory::Change saved;
    saved.kind  = DocumentHistory::Kind::Saved;
    saved.count = m_pageCount;
    saved.text  = QFileInfo(path).fileName();
    recordChange(saved, m_filePath);
}

void DocumentView::recordChange(const DocumentHistory::Change &c,
                                const QString &snapshotSource)
{
    // Restoring a state walks the document through changes it has already
    // recorded — writing them down again would append the past to the log.
    if (m_restoring || m_filePath.isEmpty()) return;
    m_history->record(c, m_undoStack->index(), imageStates(), snapshotSource);
}

void DocumentView::noteDocumentOpened()
{
    if (m_restoring) return;

    if (m_openChange.kind != DocumentHistory::Kind::Opened) {
        // A change produced this file (page operations). The history keeps
        // running; the file itself becomes the state to come back to.
        m_history->record(m_openChange, m_undoStack->index(), imageStates(),
                          m_filePath);
        return;
    }

    // A document the user opened: everything recorded so far belongs to the
    // one before it.
    m_history->reset();
    DocumentHistory::Change opened;
    opened.kind  = DocumentHistory::Kind::Opened;
    opened.count = m_pageCount;
    opened.text  = QFileInfo(currentFile()).fileName();
    // The file is on disk and nothing has touched it — no reason to copy it
    // until something is about to.
    m_history->record(opened, m_undoStack->index(), imageStates(), m_filePath,
                      DocumentHistory::Snapshot::Deferred);
}

bool DocumentView::restoreHistoryState(int index)
{
    if (!m_history->canRestore(index)) return false;
    if (index == m_history->currentIndex()) return true;

    const DocumentHistory::Entry entry = m_history->entries().value(index);

    if (m_history->restoringDropsEdits(index)) {
        // The state lives in a different document file. It is reopened from a
        // copy, never from the snapshot itself — a snapshot that becomes the
        // file being edited would be modified by the next save, and the state
        // it stands for would be gone.
        const QString snapshot = m_history->baseFileFor(index);
        const QString work     = SessionStore::newWorkingFile(currentFile());
        if (snapshot.isEmpty() || work.isEmpty()) return false;
        QFile::remove(work);
        if (!QFile::copy(snapshot, work)) return false;
#ifdef HAVE_PDF_RENDERING
        PdfPwStore::set(work, PdfPwStore::get(currentFile()));
#endif

        const QString target = currentFile();
        m_restoring = true;
        const bool ok = openWorkingCopy(work, target);
        m_restoring = false;
        if (!ok) {
            SessionStore::discard(work);
            return false;
        }
        // The reopened file carries the page state and the entry carries the
        // images; only the text edits of that moment are gone, because they
        // were never written to any file. The user agreed to that before we
        // got here (see DocumentHistory::restoringDropsEdits).
#ifdef HAVE_PDF_RENDERING
        QList<ImageAnnotationLayer::Placed> restored;
        restored.reserve(entry.images.size());
        for (const DocumentHistory::ImageState &s : entry.images)
            restored.append({ s.page, s.pdfBounds, s.image });
        m_imageLayer->restoreImages(restored);
#endif
        m_history->setCurrentIndex(index);
        m_workingCopyDirty = true;
        return true;
    }

    // Same file: the state is a position in the session. The undo stack holds
    // the text edits, the entry itself holds the images.
    m_restoring = true;
    m_undoStack->setIndex(entry.undoIndex);
#ifdef HAVE_PDF_RENDERING
    QList<ImageAnnotationLayer::Placed> images;
    images.reserve(entry.images.size());
    for (const DocumentHistory::ImageState &s : entry.images)
        images.append({ s.page, s.pdfBounds, s.image });
    m_imageLayer->restoreImages(images);
#endif
    m_restoring = false;

    m_history->setCurrentIndex(index);
    return true;
}

bool DocumentView::hasUnsavedEdits() const
{
    // Page changes live in the working copy, not in the session — without this
    // the close prompt would let a reorganized document go unsaved silently.
    if (m_workingCopyDirty) return true;
#ifdef HAVE_PDF_RENDERING
    if (!m_session) return false;
    // Text edits: the undo stack knows whether the document is back at the
    // state that was written. Images carry no undo command, so they are
    // compared against the revision the save recorded.
    return !m_undoStack->isClean()
        || m_session->imageRevision() != m_savedImageRevision;
#else
    return false;
#endif
}

#ifdef HAVE_POPPLER
bool DocumentView::writePopplerRaster(const QString &outputPath)
{
    if (!m_popplerDoc || !m_renderer || !m_session || m_pageCount <= 0)
        return false;

    constexpr qreal kPts2Px = 300.0 / 72.0;  // 300 DPI

    QPdfWriter writer(outputPath);
    writer.setCreator(QStringLiteral("OpenPDF Studio"));
    writer.setResolution(300);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));

    // Page size is per page, not per document: a size taken from page 1 and
    // kept squeezes every differently sized page (a landscape sheet, a scan)
    // onto that first box. It has to be set before the page it applies to
    // begins — for the first page that means before the painter starts it.
    const auto setSizeFor = [&writer](const QSizeF &pts) {
        writer.setPageSize(QPageSize(pts, QPageSize::Point, QString(),
                                     QPageSize::ExactMatch));
    };
    setSizeFor(m_renderer->pageSizePts(0));

    QPainter painter(&writer);
    if (!painter.isActive()) return false;

    for (int i = 0; i < m_pageCount; ++i) {
        if (i > 0) {
            setSizeFor(m_renderer->pageSizePts(i));
            if (!writer.newPage()) return false;
        }
        QImage img = m_renderer->renderPage(i, kPts2Px);
        if (img.isNull()) continue;
        m_session->applyToImage(i, img, kPts2Px);
        const QRect pageRect(0, 0, painter.device()->width(), painter.device()->height());
        painter.drawImage(pageRect, img);
    }
    painter.end();
    return true;
}

std::unique_ptr<Poppler::Document> DocumentView::loadPopplerDocument(const QString &path)
{
    if (path.isEmpty()) return nullptr;
    auto doc = Poppler::Document::load(path);
    if (doc && doc->isLocked()) {
        const QByteArray known = PdfPwStore::get(path).toUtf8();
        if (!known.isEmpty()) doc->unlock(known, known);
    }
    if (!doc || doc->isLocked()) return nullptr;
    doc->setRenderHint(Poppler::Document::Antialiasing);
    doc->setRenderHint(Poppler::Document::TextAntialiasing);
    return doc;
}

void DocumentView::setPopplerSource(std::unique_ptr<Poppler::Document> doc)
{
    m_contentProvider.reset();   // references the old doc — drop it first
    // Drop the handles into m_renderer before the object behind it dies.
    m_selection->setSource(nullptr, nullptr);
    m_layoutEngine->setSource(nullptr, m_session);
    m_imageLayer->setSource(nullptr, m_session, m_ocrEngine, QString());
    delete m_renderer;
    m_renderer = nullptr;

    m_popplerDoc = std::move(doc);
    if (!m_popplerDoc) return;   // closed: the file is free to be replaced now

    m_renderer = new PdfRenderer(m_popplerDoc.get());
    m_selection->setSource(m_renderer, m_popplerDoc.get());
    m_layoutEngine->setSource(m_renderer, m_session);
    m_imageLayer->setSource(m_renderer, m_session, m_ocrEngine, m_filePath);
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
        if (m_currentEditorRenderSizePt > 0 && ptSize != m_currentEditorRenderSizePt) {
            const qreal ratio = (qreal)ptSize / m_currentEditorRenderSizePt;
            const QPointF anchor = m_activeEditBounds.topLeft();
            m_activeEditBounds = QRectF(anchor, m_activeEditBounds.size() * ratio);
            clampToPdfPage(m_activeEditPage, m_activeEditBounds);
        }
        // A size the user typed is meant literally, in the document's own
        // font — no calibration to a substitute face on top of it.
        m_currentEditorFontSizePt   = ptSize;
        m_currentEditorRenderSizePt = ptSize;
        m_editorSizeChangedByUser   = true;
        const QLabel *lbl = pageLabel(m_activeEditPage);
        if (lbl) {
            m_editorFrame->setPageRect(lbl->geometry());
            const QRectF cb(
                m_activeEditBounds.topLeft() * scale + QPointF(lbl->pos()),
                m_activeEditBounds.size() * scale);
            m_editorFrame->repositionForZoom(cb, qMax(6, qRound(ptSize * scale)));
        }
    } else {
        m_currentEditorFontSizePt   = ptSize;
        m_currentEditorRenderSizePt = ptSize;
        m_editorSizeChangedByUser   = true;
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

#ifdef HAVE_PDF_RENDERING
DocumentExporter::Sources DocumentView::exportSources() const
{
    DocumentExporter::Sources src;
    src.renderer  = m_renderer;
    src.provider  = m_contentProvider.get();
    src.session   = m_session;
    src.ocr       = m_ocrEngine;
    src.pageCount = m_pageCount;
#  ifdef HAVE_QT_PDF
    src.document  = m_document;
    src.extractor = m_extractor;
#  endif
    return src;
}
#endif

QList<DocxPage> DocumentView::allPageContent(const QList<int> &pages)
{
#ifdef HAVE_PDF_RENDERING
    return DocumentExporter(exportSources()).allPageContent(pages);
#else
    Q_UNUSED(pages)
    return {};
#endif
}

bool DocumentView::exportPagesToImages(const QString &outputPath, int quality,
                                       const QList<int> &pages)
{
#ifdef HAVE_PDF_RENDERING
    return DocumentExporter(exportSources())
        .exportPagesToImages(outputPath, quality, pages);
#else
    Q_UNUSED(outputPath) Q_UNUSED(quality) Q_UNUSED(pages)
    return false;
#endif
}

#ifdef HAVE_QT_PRINT
bool DocumentView::printDocument(QPrinter *printer, const QList<int> &pages)
{
#  ifdef HAVE_PDF_RENDERING
    return DocumentExporter(exportSources()).printPages(printer, pages);
#  else
    Q_UNUSED(printer) Q_UNUSED(pages)
    return false;
#  endif
}
#endif

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
    else
        syncVisibleRect();
}

QRect DocumentView::visibleCanvasRect() const
{
    // The canvas is a child of the viewport and scrolling moves it, so its
    // negated position is the viewport origin in canvas coordinates. Reading
    // it from the widget instead of the scrollbars also covers the case where
    // the canvas is smaller than the viewport and gets centred.
    return QRect(-m_canvas->pos(), viewport()->size());
}

void DocumentView::syncVisibleRect()
{
    if (m_viewMode != ViewMode::Single) return;
    m_layoutEngine->setVisibleRect(visibleCanvasRect());
}

void DocumentView::updateScrollRange()
{
    if (m_layout) m_layout->activate();
    // QScrollArea recomputes the canvas size and the scrollbar ranges when it
    // handles a layout request; the posted one only arrives after the current
    // event returns, which is too late for the scroll anchoring above.
    QEvent layoutRequest(QEvent::LayoutRequest);
    QCoreApplication::sendEvent(this, &layoutRequest);
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
        // Same as above: the canvas position is only meaningful after the
        // scroll area has taken the widget back.
        QMetaObject::invokeMethod(this, [this]() { syncVisibleRect(); },
                                  Qt::QueuedConnection);
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

// ── Font-size calibration ─────────────────────────────────────────────────────
// How tall text LOOKS is not decided by its point size alone: the ink-to-em
// ratio differs from font to font, and the family we paint an edit with is
// rarely the embedded one (the Poppler backend reports no family at all, and
// the vector save substitutes Standard-14 fonts). On top of that, only the
// qpdf scanner knows the real /Tf size — every other source estimates it from
// line boxes. So the size is derived from what is actually on the page: the
// measured ink height of the original glyphs.

// Vertical ink runs inside `region`: a run is a sequence of rows carrying
// pixels that stand out from the region's background, reported as its first
// row, its height in rows, and the number of ink pixels it holds.
struct InkRun { int top; int height; qint64 pixels; };

static QList<InkRun> inkRuns(const QImage &img, const QRect &region)
{
    const QRect sr = region.intersected(img.rect());
    if (sr.width() < 4 || sr.height() < 4) return {};

    const auto lumAt = [&img](int x, int y) {
        const QRgb c = pixelOverWhite(img, x, y);
        return (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000;
    };

    // Background = most frequent luminance in the region. Taking the mode (not
    // "white") keeps the measurement working on colored table rows and on dark
    // headers with light text.
    QMap<int, int> hist;
    for (int y = sr.top(); y <= sr.bottom(); ++y)
        for (int x = sr.left(); x <= sr.right(); ++x)
            hist[lumAt(x, y)]++;
    int bgLum = 255, bgN = 0;
    for (auto it = hist.cbegin(); it != hist.cend(); ++it)
        if (it.value() > bgN) { bgN = it.value(); bgLum = it.key(); }

    QList<InkRun> runs;
    int start = -1;
    qint64 pixels = 0;
    for (int y = sr.top(); y <= sr.bottom(); ++y) {
        int ink = 0;
        for (int x = sr.left(); x <= sr.right(); ++x)
            if (std::abs(lumAt(x, y) - bgLum) > 40) ++ink;
        // Two pixels per row: a single one is antialiasing noise.
        if (ink >= 2) {
            if (start < 0) { start = y; pixels = 0; }
            pixels += ink;
        } else if (start >= 0) {
            runs.append({ start, y - start, pixels });
            start = -1;
        }
    }
    if (start >= 0) runs.append({ start, sr.bottom() + 1 - start, pixels });
    return runs;
}

// Leftmost column between rows `top` and `bottom` that carries ink, or -1.
static int inkLeftPx(const QImage &img, const QRect &region, int top, int bottom)
{
    const QRect sr = region.intersected(img.rect());
    if (sr.isEmpty()) return -1;

    const auto lumAt = [&img](int x, int y) {
        const QRgb c = pixelOverWhite(img, x, y);
        return (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000;
    };
    QMap<int, int> hist;
    for (int y = sr.top(); y <= sr.bottom(); ++y)
        for (int x = sr.left(); x <= sr.right(); ++x)
            hist[lumAt(x, y)]++;
    int bgLum = 255, bgN = 0;
    for (auto it = hist.cbegin(); it != hist.cend(); ++it)
        if (it.value() > bgN) { bgN = it.value(); bgLum = it.key(); }

    const int y0 = qMax(sr.top(), top);
    const int y1 = qMin(sr.bottom(), bottom);
    for (int x = sr.left(); x <= sr.right(); ++x) {
        int ink = 0;
        for (int y = y0; y <= y1; ++y)
            if (std::abs(lumAt(x, y) - bgLum) > 40) ++ink;
        if (ink >= 1) return x;
    }
    return -1;
}

// The main ink run inside `region` — the run holding the most ink, which for
// text is the body of the letters. `top` is its first row (-1 = nothing found),
// `height` its extent, `left` the first column carrying ink within it.
//
// Both sides of the calibration go through this same function, and that is the
// whole point: page and probe must be measured alike, down to which part of the
// glyphs counts. Taking the densest run rather than the full ink extent is what
// makes that possible — an accent sits beyond a gap that is indistinguishable
// from tight line spacing, so no honest rule could include it on the page side.
// The caller's job is to hand this function a region with room to spare, or the
// run comes back clipped on one side only (see measuredInkPt).
struct InkLine { int top { -1 }; int height { 0 }; int left { -1 }; };

static InkLine inkLinePx(const QImage &img, const QRect &region)
{
    const QList<InkRun> runs = inkRuns(img, region);
    if (runs.isEmpty()) return {};

    // Ink count, not height: it keeps accents, rules and cell borders from
    // being mistaken for the text — they are thin AND sparse.
    const InkRun *main = &runs.first();
    for (const InkRun &r : runs)
        if (r.pixels > main->pixels) main = &r;

    InkLine out;
    out.top    = main->top;
    out.height = main->height;
    out.left   = inkLeftPx(img, region, main->top, main->top + main->height - 1);
    return out;
}

// The ink of one text line inside `boundsPt`, in PDF points, measured on a
// render of the page at `scale` px/pt. height 0 = nothing measurable found.
struct MeasuredInk { double top { 0.0 }; double height { 0.0 }; double left { 0.0 }; };

static MeasuredInk measuredInkPt(const QImage &img, const QRectF &boundsPt,
                                 qreal scale)
{
    if (img.isNull() || boundsPt.isEmpty() || scale <= 0.0) return {};
    const auto measure = [&](const QRectF &r) -> MeasuredInk {
        const QRectF px(r.topLeft() * scale, r.size() * scale);
        const InkLine ink = inkLinePx(img, px.toAlignedRect());
        if (ink.top < 0) return {};
        return { ink.top / scale, ink.height / scale,
                 ink.left >= 0 ? ink.left / scale : r.left() };
    };

    const MeasuredInk tight = measure(boundsPt);
    if (tight.height <= 0.0) return tight;

    // A line box cuts accents and descenders off, so measuring inside it
    // reports LESS ink than the text really has — while the reference this is
    // compared against is drawn with room to spare. That gap alone shrinks
    // every edited line by a few percent. Measuring again with headroom closes
    // it, as long as the extra rows did not drag a neighbouring line in: a
    // second line of text roughly doubles the run, real accents add a fraction.
    const double pad = qMax(1.0, boundsPt.height() * 0.25);
    const MeasuredInk padded = measure(boundsPt.adjusted(0, -pad, 0, pad));
    if (padded.height > tight.height && padded.height <= tight.height * 1.35)
        return padded;
    return tight;
}

// What `f` puts on the page, per 1 pt of font size, when the text is drawn with
// its pen at the origin: how tall the ink is, how far above the baseline it
// starts, and how far right of the pen it starts.
//
// It DRAWS the text and measures the result instead of asking the font for its
// metrics: a tight bounding rect is only as truthful as the platform's font
// backend, and where that backend has no font at all it reports a full em box
// while painting something much smaller. Rendering compares what the user will
// actually see against what the page actually shows.
//
// The last two numbers turn a measured ink position back into a pen position,
// which is how backends that cannot read the text matrix (Poppler) still get an
// anchor to put replacement text on.
struct FontInk {
    double heightPerPt { 0.0 };   // ink height
    double risePerPt   { 0.0 };   // baseline → ink top (positive = above)
    double bearingPerPt{ 0.0 };   // pen x → ink left
};

static FontInk fontInkPerPt(const QString &text, QFont f)
{
    constexpr int kRef = 64;   // large enough that hinting rounding is noise
    f.setPixelSize(kRef);
    const QFontMetricsF fm(f);

    QList<double> heights, rises, bearings;
    const QStringList lines = text.split(u'\n');
    for (const QString &raw : lines) {
        // The vertical extremes come from a handful of glyphs; capping the
        // length keeps the probe image small on very long lines.
        const QString ln = raw.trimmed().left(120);
        if (ln.isEmpty()) continue;

        const int w = qBound(4 * kRef,
                             qCeil(fm.horizontalAdvance(ln)) + 2 * kRef,
                             8000);
        // Pen at (kRef/2, 2*kRef) — drawn from the baseline, so the measured
        // ink box is directly relative to the anchor the caller needs.
        constexpr int kPenX = kRef / 2;
        constexpr int kBase = 2 * kRef;
        QImage probe(w, 4 * kRef, QImage::Format_RGB32);
        probe.fill(Qt::white);
        {
            QPainter p(&probe);
            p.setFont(f);
            p.setPen(Qt::black);
            p.drawText(QPointF(kPenX, kBase), ln);
        }
        const InkLine ink = inkLinePx(probe, probe.rect());
        if (ink.height > 0 && ink.top >= 0) {
            heights.append(ink.height / double(kRef));
            rises.append((kBase - ink.top) / double(kRef));
            if (ink.left >= 0)
                bearings.append((ink.left - kPenX) / double(kRef));
        }
    }
    if (heights.isEmpty()) return {};

    const auto median = [](QList<double> v) {
        if (v.isEmpty()) return 0.0;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    return { median(heights), median(rises), median(bearings) };
}

void DocumentView::handleEditClick(const QPoint &canvasPos)
{
#ifdef HAVE_PDF_RENDERING
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
    m_activeEditPdfText      = isSessionEdit ? sessionEdit.originalText : displayText;
    if (isSessionEdit)
        m_activeEditFieldName = sessionEdit.formField;

    // Page rendered at 3 px per pt — shared by the font-size calibration and
    // the color samplers below, rendered lazily and at most once. Low-dpi
    // renders consist mostly of antialiasing pixels and make both unreliable.
    constexpr qreal kSampleScale = 3.0;
    QImage sampImg;
    const auto sampleImage = [&]() -> const QImage & {
        if (sampImg.isNull())
            sampImg = m_renderer->renderPage(block.page, kSampleScale);
        return sampImg;
    };

    // Font size: stored session value > region-model detection > line-height
    // estimate from the block bounds.
    // sizeIsExact tracks whether the result is the size the PDF itself states.
    // Only the qpdf scanner can know it, and only then may it be written back
    // to the file unchanged — everything else is an estimate that has to be
    // calibrated against the rendered ink further down.
    bool sizeIsExact = false;
    m_editorSizeChangedByUser = false;
    if (isSessionEdit && sessionEdit.fontSizePt > 0.0) {
        m_currentEditorFontSizePt = qMax(4.0, sessionEdit.fontSizePt);
        m_editorSizeChangedByUser = sessionEdit.sizeChanged;
        sizeIsExact = true;               // already settled when it was created
    } else {
        const double detectedPt = (contentItem.isValid() && contentItem.fontSizePt > 0.0)
                                      ? contentItem.fontSizePt : 0.0;
        if (detectedPt > 0.0 && detectedPt <= 144.0) {
            const double polyEst   = block.pdfBounds.height() / 0.72;
            const bool   plausible = (detectedPt <= polyEst * 4.0)
                                   || (block.pdfBounds.height() >= 20.0);
            if (plausible) {
                // Kept fractional: rounding a 16.5 pt heading down to 16 is a
                // visible shrink, and it is the number written back to the file.
                m_currentEditorFontSizePt = qMax(4.0, detectedPt);
                sizeIsExact = contentItem.fontSizeExact;
            } else {
                m_currentEditorFontSizePt = qMax(4.0, qMin(polyEst, 28.0));
            }
        } else {
            const int    lineCount = qMax(1, displayText.count(u'\n') + 1);
            const double lineH     = qMin(m_activeEditBounds.height() / lineCount, 20.0);
            m_currentEditorFontSizePt = qMax(4.0, lineH / 0.72);
        }
    }
    // Sanity: the clicked line's glyph height caps the font size — a
    // detection outlier must not commit 2-3x oversized text.
    if (!isSessionEdit && !contentItem.isFormField()) {
        const double lineGlyphH = block.pdfBounds.height();
        if (lineGlyphH > 4.0 && lineGlyphH < 60.0
                && m_currentEditorFontSizePt > lineGlyphH * 1.5) {
            m_currentEditorFontSizePt = qMax(4.0, lineGlyphH * 1.05);
            sizeIsExact = false;          // overridden → back to an estimate
        }
    }

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
    // Calibrate against the ORIGINAL ink (see the helpers above). What must be
    // preserved is how tall the text LOOKS, and the point size alone does not
    // decide that: the editor paints with a different family than the document
    // (always so on the Poppler backend, which reports none) and equal point
    // sizes render visibly different ink there. Editing a line must never
    // resize it, so the size we PAINT with is the one whose ink height matches
    // what the page actually shows.
    //
    // That fitted size is not the one the file gets. An exact size is what the
    // PDF itself states, and the vector save must write it back untouched — a
    // size bent to fit a substitute face would be wrong in the document's own
    // font. So the two part ways here: fitted on screen, exact on disk. Only an
    // estimated size, which is nothing but a guess, is replaced outright.
    m_currentEditorRenderSizePt = m_currentEditorFontSizePt;
    const bool measurable = !isSessionEdit && !contentItem.isFormField()
                         && !displayText.isEmpty() && !sampleImage().isNull();
    MeasuredInk origInk;
    FontInk     probeInk;
    if (measurable) {
        origInk = measuredInkPt(sampleImage(), m_activeEditBounds, kSampleScale);
        QFont probe(m_currentEditorFontFamily.isEmpty()
                        ? QStringLiteral("Helvetica") : m_currentEditorFontFamily);
        probe.setStyleHint(QFont::SansSerif);
        probe.setBold(m_currentEditorBold);
        probe.setItalic(m_currentEditorItalic);
        probeInk = fontInkPerPt(displayText, probe);
        if (origInk.height > 1.0 && probeInk.heightPerPt > 0.05) {
            // The measurement corrects the estimate, it does not replace it:
            // ink that can't be told apart from its surroundings (text over an
            // image, a band of graphics inside the bounds) must not be able to
            // blow the size up or collapse it.
            const double fitted = origInk.height / probeInk.heightPerPt;
            m_currentEditorRenderSizePt =
                qBound(qMax(4.0, m_currentEditorFontSizePt * 0.6),
                       fitted,
                       qMax(5.0, m_currentEditorFontSizePt * 1.5));
            if (!sizeIsExact)
                m_currentEditorFontSizePt = m_currentEditorRenderSizePt;
        }
    }

    // Anchor for the replacement text: where the original actually starts,
    // x on the pen and y on the BASELINE of its FIRST line. The content scanner
    // reads it straight out of the text matrix; where no scanner exists
    // (Poppler), the probe above just measured how far our own font puts ink
    // above and right of its pen, so the first line's measured ink position
    // converts back into a pen position.
    m_activeEditLineSpacingPt = isSessionEdit ? sessionEdit.lineSpacingPt
                                              : contentItem.lineSpacingPt;

    m_activeEditHasOrigin = false;
    QPointF textOrigin;
    if (isSessionEdit) {
        m_activeEditHasOrigin = sessionEdit.hasTextOrigin;
        textOrigin = sessionEdit.pdfBounds.topLeft() + sessionEdit.textOriginOffset;
    } else if (!contentItem.textOrigin.isNull()
               && contentItem.textOrigin.y() >= m_activeEditBounds.top() - 1.0
               && contentItem.textOrigin.y()
                      <= m_activeEditBounds.top() + m_currentEditorRenderSizePt * 1.6) {
        // Guarded against a paragraph whose first line is not the one the
        // scanner reported — anchoring line 1 to line 3's baseline would drop
        // the whole block down the page.
        m_activeEditHasOrigin = true;
        textOrigin = contentItem.textOrigin;
    } else if (measurable && probeInk.risePerPt > 0.05) {
        const MeasuredInk first = measuredInkPt(
            sampleImage(),
            QRectF(m_activeEditBounds.left(), m_activeEditBounds.top(),
                   m_activeEditBounds.width(),
                   qMin(m_activeEditBounds.height(),
                        m_currentEditorRenderSizePt * 1.5)),
            kSampleScale);
        if (first.height > 1.0) {
            m_activeEditHasOrigin = true;
            textOrigin = QPointF(
                first.left - probeInk.bearingPerPt * m_currentEditorRenderSizePt,
                first.top  + probeInk.risePerPt    * m_currentEditorRenderSizePt);
        }
    }

    Q_EMIT editorFontSizeChanged(qRound(m_currentEditorFontSizePt));

    Q_EMIT editorFontChanged(m_currentEditorFontFamily.isEmpty()
                                 ? QStringLiteral("Helvetica")
                                 : m_currentEditorFontFamily,
                             m_currentEditorBold, m_currentEditorItalic);

    // Expand edit bounds to cover the full rendered line height.  Glyph
    // polygons capture only the inked area (cap height ≈ 72% of font size);
    // large headings can leave original characters peeking out beneath the
    // blank fill without this. Sizes here are the ones being PAINTED — this
    // is about what the frame has to cover on screen.
    if (m_currentEditorRenderSizePt > 0) {
        const double minH = m_currentEditorRenderSizePt * 1.15;
        if (m_activeEditBounds.height() < minH) {
            m_activeEditBounds.setHeight(minH);
            clampToPdfPage(m_activeEditPage, m_activeEditBounds);
        }
        // Hard cap: no single-line edit frame should exceed 5× the detected
        // font size — catches polygon bounds that span whole table blocks.
        // Multi-line paragraph bounds from the region model are trusted.
        if (!paragraphBounds) {
            const double capH = m_currentEditorRenderSizePt * 5.0;
            if (m_activeEditBounds.height() > capH) {
                m_activeEditBounds.setHeight(capH);
                clampToPdfPage(m_activeEditPage, m_activeEditBounds);
            }
        }
        m_activeEditOriginalBounds = m_activeEditBounds;
    }

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

    const int fontSize = qMax(6, qRound(m_currentEditorRenderSizePt * scale));

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
    if (m_currentEditorRenderSizePt > 0 && !displayText.isEmpty()) {
        QFont measure(m_currentEditorFontFamily.isEmpty()
                          ? QStringLiteral("Helvetica")
                          : m_currentEditorFontFamily);
        measure.setStyleHint(QFont::SansSerif);
        // 1 px == 1 pt here
        measure.setPixelSize(qMax(1, qRound(m_currentEditorRenderSizePt)));
        measure.setBold(m_currentEditorBold);
        measure.setItalic(m_currentEditorItalic);
        const QFontMetricsF fm(measure);
        qreal needW = 0.0;
        const QStringList lines = displayText.split(u'\n');
        for (const QString &ln : lines)
            needW = qMax(needW, fm.horizontalAdvance(ln));
        needW += m_currentEditorRenderSizePt * 0.6;   // caret/AA headroom
        if (needW > m_activeEditBounds.width())
            m_activeEditBounds.setWidth(
                qMin(needW, pageSize.width() - m_activeEditBounds.left() - 2.0));
    }

    // Freeze the anchor against the final box. Stored as an offset, it now
    // rides along with every later move or resize of the frame for free.
    m_activeEditOriginOffset = m_activeEditHasOrigin
                                   ? textOrigin - m_activeEditBounds.topLeft()
                                   : QPointF();

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

    // Reference state for the "nothing changed" check in commitCurrentEdit.
    // Captured AFTER present(): the frame grows to fit its content on open and
    // reports the grown geometry back through boundsChanged, so this is the
    // resting state — any later deviation really is the user's doing.
    m_activeEditInPlace             = true;
    m_activeEditPresentedBounds     = m_activeEditBounds;
    m_activeEditPresentedFontSizePt = m_currentEditorFontSizePt;
    m_activeEditPresentedColor      = m_currentEditorColor;
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
    m_savedImageRevision = m_session->imageRevision();
}

bool DocumentView::detachSourceFrom(const QString &saveTarget)
{
    if (m_filePath.isEmpty() || saveTarget.isEmpty()) return false;
    // Already on a working copy — the save cannot reach it.
    if (SessionStore::isWorkingFile(m_filePath)) return true;
    // Writing somewhere else entirely leaves the base alone.
    if (QFileInfo(m_filePath).absoluteFilePath()
            != QFileInfo(saveTarget).absoluteFilePath())
        return true;

    const QString work = SessionStore::newWorkingFile(m_filePath);
    if (work.isEmpty()) return false;
    QFile::remove(work);
    if (!QFile::copy(m_filePath, work)) return false;
    PdfPwStore::set(work, PdfPwStore::get(m_filePath));

    const QString original = m_filePath;
#ifdef HAVE_QT_PDF
    m_document->close();
    m_document->setPassword(PdfPwStore::get(work));
    if (m_document->load(work) != QPdfDocument::Error::None) {
        m_document->close();
        m_document->setPassword(PdfPwStore::get(original));
        m_document->load(original);
        SessionStore::discard(work);
        return false;
    }
#elif defined(HAVE_POPPLER)
    auto copy = loadPopplerDocument(work);
    if (!copy) { SessionStore::discard(work); return false; }
    setPopplerSource(std::move(copy));
#endif
    m_filePath = work;
    // The content scanner holds the old path open; on Windows that alone would
    // block the save from replacing it.
    resetContentProvider();
    return true;
}

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
    e.renderSizePt = m_currentEditorRenderSizePt;
    e.textOriginOffset = m_activeEditOriginOffset;
    e.hasTextOrigin    = m_activeEditHasOrigin;
    e.lineSpacingPt    = m_activeEditLineSpacingPt;
    e.originalText     = m_activeEditPdfText;
    e.textColor   = m_currentEditorColor;
    e.bgColor     = m_currentBgColor;
    e.fontFamily  = m_currentEditorFontFamily;
    e.bold        = m_currentEditorBold;
    e.italic      = m_currentEditorItalic;
    e.fontChanged = m_editorFontChangedByUser;
    e.sizeChanged = m_editorSizeChangedByUser;
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

    // Nothing changed → drop the edit instead of committing it. Committing
    // would erase the original glyphs and re-draw the text with OUR font, so a
    // click that only opened and closed the editor would visibly rewrite the
    // line (different family, ligatures and umlauts gone if the text came from
    // OCR). The document must stay byte-identical unless the user really
    // edited something.
    if (m_activeEditInPlace) {
        // Bounds come back from integer widget geometry, so compare with a
        // tolerance of about two screen pixels — anything the user actually
        // dragged or resized moves much further than that.
        const double tol = qMax(1.0, 2.0 / PdfRenderer::screenScale(m_zoom));
        const auto nearly = [tol](const QRectF &a, const QRectF &b) {
            return std::abs(a.left()   - b.left())   < tol
                && std::abs(a.top()    - b.top())    < tol
                && std::abs(a.width()  - b.width())  < tol
                && std::abs(a.height() - b.height()) < tol;
        };
        const bool untouched =
               page == srcPage
            && trimNew == m_activeEditOriginalText.trimmed()
            && nearly(bounds, m_activeEditPresentedBounds)
            && !m_editorFontChangedByUser
            && qFuzzyCompare(m_currentEditorFontSizePt + 1.0,
                             m_activeEditPresentedFontSizePt + 1.0)
            && m_currentEditorColor      == m_activeEditPresentedColor;
        if (untouched) {
            m_activeEditInPlace = false;
            m_activeEditFieldName.clear();
            m_session->restoreSuspended();   // undo the open-time suspension
            m_lastCommittedPage = -1;        // nothing committed to protect
            rerenderPage(srcPage);           // drops the live blank fill
            return;
        }
    }
    m_activeEditInPlace = false;

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
    if (snapAfter != snapBefore) {
        // A push moves the stack index too, but it is a NEW state — the
        // history must append it, not look for one it already knows.
        m_pushingEdit = true;
        m_undoStack->push(new EditUndoCmd(m_session, this, srcPage, page,
                                          snapBefore, snapAfter));
        m_pushingEdit = false;
        // Recorded after the push so the entry carries the stack index the
        // state sits at — that index is what takes the document back here.
        recordChange({ trimNew.isEmpty() ? DocumentHistory::Kind::TextRemoved
                                         : DocumentHistory::Kind::TextEdited,
                       page });
    }

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
    m_activeEditInPlace    = false;
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
    m_activeEditPdfText       = QString();  // nothing replaced → no font to keep
    m_activeEditInPlace       = false;   // fresh box — there is nothing to leave alone
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
    m_editorSizeChangedByUser = false;
    m_currentEditorFontSizePt   = 12.0;
    m_currentEditorRenderSizePt = 12.0;
    // A box drawn on empty canvas has no original text to sit on the line of.
    m_activeEditHasOrigin     = false;
    m_activeEditOriginOffset  = QPointF();
    m_activeEditLineSpacingPt = 0.0;
    Q_EMIT editorFontSizeChanged(qRound(m_currentEditorFontSizePt));
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
