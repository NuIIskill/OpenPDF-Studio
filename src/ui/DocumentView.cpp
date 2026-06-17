#include "DocumentView.hpp"

#include <QVBoxLayout>
#include <QLabel>
#include <QRubberBand>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QScrollBar>
#include <QMouseEvent>
#include <QFrame>
#include <QPalette>
#include <QPainter>
#include <QApplication>
#include <QDebug>

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
    viewport()->installEventFilter(this);
    m_canvas->installEventFilter(this);     // also catch events on the canvas itself
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());
    retranslateUi();

#ifdef HAVE_QT_PDF
    m_document  = new QPdfDocument(this);
    m_renderer  = new PdfRenderer(m_document);
    m_extractor = new PdfTextExtractor(m_document);
    m_session   = new EditSession();

    m_editorFrame = new TextBoxFrame(m_canvas);
    connect(m_editorFrame, &TextBoxFrame::committed, this, &DocumentView::commitCurrentEdit);
    connect(m_editorFrame, &TextBoxFrame::cancelled,  this, &DocumentView::cancelCurrentEdit);
    connect(m_editorFrame, &TextBoxFrame::boundsChanged, this, [this](const QRectF &inner) {
        if (m_activeEditPage < 0) return;
        const QLabel *lbl = m_pageLabels.value(m_activeEditPage, nullptr);
        if (!lbl) return;
        const qreal scale = PdfRenderer::screenScale(m_zoom);
        m_activeEditBounds = QRectF(
            (inner.topLeft() - QPointF(lbl->pos())) / scale,
            inner.size() / scale);
    });
#endif
    // HAVE_POPPLER: m_renderer and m_popplerDoc are created per-file in openFile()

    m_ocrEngine = new OcrEngine();
}

DocumentView::~DocumentView()
{
    delete m_ocrEngine;
#ifdef HAVE_QT_PDF
    delete m_renderer;
    delete m_extractor;
    delete m_session;
#elif defined(HAVE_POPPLER)
    delete m_renderer;
    // m_popplerDoc (unique_ptr) cleaned up automatically
#endif
}

// ── File ──────────────────────────────────────────────────────────────────────

void DocumentView::clearDocument()
{
    cancelCurrentEdit();
#ifdef HAVE_QT_PDF
    m_session->clear();
    m_document->close();
    m_ocrCache.clear();
#elif defined(HAVE_POPPLER)
    delete m_renderer;
    m_renderer = nullptr;
    m_popplerDoc.reset();
#endif
    m_filePath.clear();
    m_pageCount = 0;
    m_undoStack->clear();

    for (QLabel *lbl : m_pageLabels) {
        m_layout->removeWidget(lbl);
        delete lbl;
    }
    m_pageLabels.clear();
    m_dropHint->show();
}

bool DocumentView::openFile(const QString &path)
{
    if (path.isEmpty()) return false;
    cancelCurrentEdit();

#ifdef HAVE_QT_PDF
    m_session->clear();
    const auto err = m_document->load(path);
    if (err != QPdfDocument::Error::None) return false;
    m_filePath  = path;
    m_pageCount = m_document->pageCount();
    buildPages();
    Q_EMIT fileOpened(m_filePath, m_pageCount);
    Q_EMIT pageChanged(1, m_pageCount);
    return true;

#elif defined(HAVE_POPPLER)
    auto doc = Poppler::Document::load(path);
    if (!doc || doc->isLocked()) return false;
    doc->setRenderHint(Poppler::Document::Antialiasing);
    doc->setRenderHint(Poppler::Document::TextAntialiasing);
    delete m_renderer;
    m_popplerDoc = std::move(doc);
    m_renderer   = new PdfRenderer(m_popplerDoc.get());
    m_filePath   = path;
    m_pageCount  = m_popplerDoc->numPages();
    buildPages();
    Q_EMIT fileOpened(m_filePath, m_pageCount);
    Q_EMIT pageChanged(1, m_pageCount);
    return true;

#else
    m_filePath  = path;
    m_pageCount = 1;
    m_dropHint->show();
    retranslateUi();
    Q_EMIT fileOpened(m_filePath, m_pageCount);
    Q_EMIT pageChanged(1, m_pageCount);
    return true;
#endif
}

// ── Zoom / Tool / Edit mode ───────────────────────────────────────────────────

void DocumentView::setZoom(int percent)
{
    if (m_zoom == percent) return;
    m_zoom = percent;
    if (!m_filePath.isEmpty()) rerenderAll();
}

void DocumentView::setTool(Tool tool)
{
    m_tool = tool;
    switch (tool) {
    case Tool::Pan:    viewport()->setCursor(Qt::OpenHandCursor); break;
    case Tool::Text:   viewport()->setCursor(Qt::IBeamCursor);    break;
    case Tool::Select: viewport()->setCursor(Qt::CrossCursor);    break;
    default:           viewport()->setCursor(Qt::ArrowCursor);    break;
    }
}

void DocumentView::setEditMode(bool on)
{
    if (m_editMode == on) return;
    cancelCurrentEdit();
    m_editMode = on;
    if (!on) setTool(m_tool); // restore tool cursor when leaving edit mode
}

bool DocumentView::saveToFile(const QString &path)
{
#ifdef HAVE_QT_PDF
    cancelCurrentEdit();
    if (!m_session->saveToFile(path, m_document, m_pageCount, m_filePath))
        return false;

    // Reload from the saved file so subsequent saves and re-edits work on
    // the updated PDF (with our Helvetica replacement text) rather than the original.
    m_session->clear();
    m_filePath = path;
    m_document->close();
    m_document->load(path);
    rerenderAll();
    return true;
#else
    Q_UNUSED(path)
    return false;
#endif
}

bool DocumentView::hasUnsavedEdits() const
{
#ifdef HAVE_QT_PDF
    return m_session->hasAnyEdits();
#else
    return false;
#endif
}

void DocumentView::setEditorFontSize(int ptSize)
{
#ifdef HAVE_QT_PDF
    if (ptSize < 4 || ptSize > 400) return;
    m_currentEditorFontSizePt = ptSize;
    if (m_activeEditPage >= 0 && m_editorFrame->isVisible()) {
        const qreal scale = PdfRenderer::screenScale(m_zoom);
        m_editorFrame->setFontSize(qMax(6, qRound(ptSize * scale)));
    }
#else
    Q_UNUSED(ptSize)
#endif
}

bool DocumentView::pdfRenderingAvailable() const
{
#ifdef HAVE_PDF_RENDERING
    return true;
#else
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

// ── Page building ─────────────────────────────────────────────────────────────

void DocumentView::buildPages()
{
    for (QLabel *lbl : m_pageLabels) {
        m_layout->removeWidget(lbl);
        delete lbl;
    }
    m_pageLabels.clear();
    m_dropHint->hide();

    for (int i = 0; i < m_pageCount; ++i) {
        auto *lbl = new QLabel(m_canvas);
        lbl->setObjectName(QStringLiteral("PageLabel"));
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setFrameStyle(QFrame::Box | QFrame::Plain);
        lbl->setLineWidth(1);
        lbl->setAutoFillBackground(true);
        QPalette p = lbl->palette();
        p.setColor(QPalette::Window, Qt::white);
        lbl->setPalette(p);
        lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_layout->addWidget(lbl, 0, Qt::AlignHCenter);
        m_pageLabels.append(lbl);
    }

    rerenderAll();
}

void DocumentView::rerenderAll()
{
#ifdef HAVE_PDF_RENDERING
    if (!m_renderer) return;
    const qreal dpr   = devicePixelRatioF();
    const qreal scale = PdfRenderer::screenScale(m_zoom);
    for (int i = 0; i < m_pageLabels.size(); ++i) {
        const QSize sz = m_renderer->pageDisplaySize(i, m_zoom);
        m_pageLabels[i]->setFixedSize(sz);
        QImage img = m_renderer->renderPage(i, scale * dpr);
        img.setDevicePixelRatio(dpr);
#ifdef HAVE_QT_PDF
        m_session->applyToImage(i, img, scale * dpr);
#endif
        if (!img.isNull())
            m_pageLabels[i]->setPixmap(QPixmap::fromImage(std::move(img)));
    }
#endif
}

void DocumentView::rerenderPage(int page)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_renderer || page < 0 || page >= m_pageLabels.size()) return;
    const qreal dpr   = devicePixelRatioF();
    const qreal scale = PdfRenderer::screenScale(m_zoom);
    QImage img = m_renderer->renderPage(page, scale * dpr);
    img.setDevicePixelRatio(dpr);
#ifdef HAVE_QT_PDF
    m_session->applyToImage(page, img, scale * dpr);
#endif
    if (!img.isNull())
        m_pageLabels[page]->setPixmap(QPixmap::fromImage(std::move(img)));
#endif
}

// Re-renders the page normally then paints a white blank over pdfBoundsPts.
// Called when starting an edit so the original text disappears from view —
// the editor widget sits over a clean white area instead of over the old text.
void DocumentView::rerenderPageWithBlank(int page, const QRectF &pdfBoundsPts)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_renderer || page < 0 || page >= m_pageLabels.size()) return;
    const qreal dpr   = devicePixelRatioF();
    const qreal scale = PdfRenderer::screenScale(m_zoom);
    QImage img = m_renderer->renderPage(page, scale * dpr);
    img.setDevicePixelRatio(dpr);
#ifdef HAVE_QT_PDF
    m_session->applyToImage(page, img, scale * dpr);
    // Erase the active-edit region so the editor sits over a blank area.
    QPainter p(&img);
    const QRectF px(pdfBoundsPts.topLeft() * scale * dpr,
                    pdfBoundsPts.size()    * scale * dpr);
    p.fillRect(px.adjusted(-3, -3, 3, 3).toAlignedRect(), Qt::white);
#endif
    if (!img.isNull())
        m_pageLabels[page]->setPixmap(QPixmap::fromImage(std::move(img)));
#endif
}

// ── Edit mode ─────────────────────────────────────────────────────────────────

std::pair<int, QLabel *> DocumentView::pageAtCanvasPos(const QPoint &canvasPos) const
{
    for (int i = 0; i < m_pageLabels.size(); ++i)
        if (m_pageLabels[i]->geometry().contains(canvasPos))
            return { i, m_pageLabels[i] };
    return { -1, nullptr };
}

void DocumentView::handleEditClick(const QPoint &canvasPos)
{
#ifdef HAVE_QT_PDF
    qWarning() << "[EDIT] handleEditClick canvasPos=" << canvasPos;
    auto [pageIdx, pageLbl] = pageAtCanvasPos(canvasPos);
    qWarning() << "[EDIT] pageIdx=" << pageIdx;
    if (pageIdx < 0) return;

    // Convert canvas position to PDF-point coordinates
    const QPointF pageLocal = QPointF(canvasPos - pageLbl->pos());
    const qreal   scale     = PdfRenderer::screenScale(m_zoom);
    const QPointF pdfPt     = pageLocal / scale;
    const QSizeF  pageSize  = m_renderer->pageSizePts(pageIdx);

    // Try native PDF text first (vector PDFs)
    TextBlock block = m_extractor->textAt(pageIdx, pdfPt, pageSize);

    // If no native text found (scanned / image PDF), fall back to OCR
    if (!block.isValid() && m_ocrEngine && m_ocrEngine->isReady()) {
        if (!m_ocrCache.contains(pageIdx)) {
            // Render at ≥300 DPI for reliable OCR accuracy
            const qreal ocrScale = qMax(scale * devicePixelRatioF(), 300.0 / 72.0);
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            const QImage pageImg = m_renderer->renderPage(pageIdx, ocrScale);
            qWarning() << "[OCR] renderPage" << pageIdx << "at scale" << ocrScale
                     << "→ image" << pageImg.size() << "(null:" << pageImg.isNull() << ")";
            m_ocrCache[pageIdx] = m_ocrEngine->recognizePage(pageImg, pageSize, ocrScale);
            QApplication::restoreOverrideCursor();
        }

        qWarning() << "[OCR] cache for page" << pageIdx << "has" << m_ocrCache[pageIdx].size() << "lines";
        qWarning() << "[OCR] click pdfPt:" << pdfPt << "pageSize:" << pageSize;
        const OcrEngine::Block ocr = OcrEngine::blockAt(m_ocrCache[pageIdx], pdfPt);
        qWarning() << "[OCR] blockAt result: valid=" << ocr.isValid()
                 << "bounds=" << ocr.pdfBounds << "text=" << ocr.text.left(60);
        if (ocr.isValid())
            block = TextBlock{ pageIdx, ocr.pdfBounds, ocr.text };
    }

    qWarning() << "[EDIT] block valid=" << block.isValid() << "text=" << block.text.left(40);
    if (!block.isValid()) return;

    // Convert block bounds back to canvas coordinates for the overlay
    const QRectF canvasBounds(
        block.pdfBounds.topLeft() * scale + QPointF(pageLbl->pos()),
        block.pdfBounds.size() * scale
    );

    // If there's already an edit at this location (user re-clicks on edited text),
    // load the current edited text rather than the original PDF text.
    const QString existing = m_session->editTextAt(block.page, block.pdfBounds);
    const QString displayText = existing.isNull() ? block.text : existing;

    m_activeEditPage         = block.page;
    m_activeEditBounds       = block.pdfBounds;
    m_activeEditOriginalText = displayText;

    // Font size: derive from single-line height (pdfBounds spans whole paragraph).
    // We store it in pt so the FormatBar and saveVector use the same value.
    const int lineCount = qMax(1, block.text.count(u'\n') + 1);
    m_currentEditorFontSizePt = qMax(4, int(block.pdfBounds.height() / lineCount * 0.78));
    Q_EMIT editorFontSizeChanged(m_currentEditorFontSizePt);

    const int fontSize = qMax(6, qRound(m_currentEditorFontSizePt * scale));
    m_editorFrame->setDecorations(true);
    m_editorFrame->resetCommitGuard();
    m_editorFrame->present(displayText, canvasBounds, fontSize);
    // Erase original text from page render so the editor isn't floating over it.
    rerenderPageWithBlank(block.page, block.pdfBounds);
#endif
}

void DocumentView::commitCurrentEdit(const QString &newText)
{
#ifdef HAVE_QT_PDF
    if (m_activeEditPage < 0) return;

    // Capture page/bounds/original before hide() — hide() can trigger
    // a recursive focusOut→committed→commitCurrentEdit call, which exits
    // early because m_activeEditPage is already -1.
    const int    page     = m_activeEditPage;
    const QRectF bounds   = m_activeEditBounds;
    const QString origText = m_activeEditOriginalText;
    m_activeEditPage = -1;

    m_editorFrame->hide();  // may trigger recursive commit, which exits early ↑

    qWarning() << "[COMMIT] page=" << page << "new=" << newText.left(40);

    const QString trimNew = newText.trimmed();
    const QString trimOld = origText.trimmed();

    if (trimNew != trimOld && !trimNew.isEmpty())
        m_session->addEdit(page, bounds, newText, m_currentEditorFontSizePt);

    rerenderPage(page);
#else
    Q_UNUSED(newText)
#endif
}

void DocumentView::cancelCurrentEdit()
{
#ifdef HAVE_QT_PDF
    const int page = m_activeEditPage;
    m_activeEditPage = -1;
    m_editorFrame->hide();
    if (page >= 0)
        rerenderPage(page);  // restore original text after cancel
#else
    m_activeEditPage = -1;
#endif
}

// ── Drag-to-create text frame ─────────────────────────────────────────────────

void DocumentView::createTextFrame(const QRect &viewportDragRect)
{
#ifdef HAVE_QT_PDF
    const QPoint scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
    const QRect canvasRect = viewportDragRect.translated(scroll);

    auto [pageIdx, pageLbl] = pageAtCanvasPos(canvasRect.center());
    if (pageIdx < 0) return;

    const qreal scale = PdfRenderer::screenScale(m_zoom);
    m_activeEditPage         = pageIdx;
    m_activeEditBounds       = QRectF(
        (QPointF(canvasRect.topLeft()) - QPointF(pageLbl->pos())) / scale,
        QSizeF(canvasRect.size()) / scale);
    m_activeEditOriginalText = QString();

    const int fontSize = qMax(8, qRound(12.0 * scale));
    m_editorFrame->setDecorations(true);  // new text box: show border + handles
    m_editorFrame->resetCommitGuard();
    m_editorFrame->present(QString(), QRectF(canvasRect), fontSize);
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
#ifdef HAVE_QT_PDF
                // Let TextBoxFrame/InlineEditor handle clicks inside the active frame.
                if (m_editorFrame->isVisible() && m_editorFrame->geometry().contains(cvsPos))
                    return QScrollArea::eventFilter(obj, e);
#endif
                cancelCurrentEdit();
                m_textDragStart = cvsPos;   // stored in canvas coords
                m_textTracking  = true;
                m_textDragging  = false;
                qWarning() << "[EF] text press cvsPos=" << cvsPos << "editMode=" << m_editMode;
                return true;
            }

            switch (m_tool) {
            case Tool::Select:
                m_selectStart = vpPos;
                m_rubberBand->setGeometry(QRect(vpPos, QSize()));
                m_rubberBand->show();
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
        if (!m_editMode) {
            const QPoint vpPos = toViewport(me->pos());
            switch (m_tool) {
            case Tool::Select:
                if (me->buttons() & Qt::LeftButton)
                    m_rubberBand->setGeometry(QRect(m_selectStart, vpPos).normalized());
                break;
            case Tool::Pan:
                if (me->buttons() & Qt::LeftButton) {
                    const QPoint d = vpPos - m_panStart;
                    horizontalScrollBar()->setValue(m_panScrollOrigin.x() - d.x());
                    verticalScrollBar()->setValue(m_panScrollOrigin.y() - d.y());
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
            if (!m_editMode) {
                switch (m_tool) {
                case Tool::Select: m_rubberBand->hide();                        break;
                case Tool::Pan:    viewport()->setCursor(Qt::OpenHandCursor);   break;
                default: break;
                }
            }
        }
    }

    return QScrollArea::eventFilter(obj, e);
}

// ── Drag & Drop ───────────────────────────────────────────────────────────────

void DocumentView::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls()) {
        for (const QUrl &url : e->mimeData()->urls())
            if (url.toLocalFile().endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
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
    }
}
