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
#include <QTimer>
#include <QDebug>
#include <QMap>

DocumentView::DocumentView(QWidget *parent)
    : QScrollArea(parent)
    , m_undoStack(new QUndoStack(this))
    , m_liveTimer(new QTimer(this))
{
    m_liveTimer->setSingleShot(true);
    m_liveTimer->setInterval(400);   // ms debounce — rerender 400ms after last keystroke
    connect(m_liveTimer, &QTimer::timeout, this, [this]() {
        liveUpdateCurrentEdit(m_livePendingText);
    });
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
    connect(m_editorFrame, &TextBoxFrame::changed, this, [this](const QString &text) {
        m_livePendingText = text;
        m_liveTimer->start();  // restart debounce window
    });
    connect(m_editorFrame, &TextBoxFrame::dragEnded, this, [this]() {
        m_liveTimer->stop();
        liveUpdateCurrentEdit(m_editorFrame->currentText());
        // Re-render page so the blank follows the box to its new position.
        // liveUpdateCurrentEdit updates the session but doesn't repaint.
        if (m_activeEditPage >= 0)
            rerenderPageWithBlank(m_activeEditPage, m_activeEditBounds);
    });
    connect(m_editorFrame, &TextBoxFrame::boundsChanged, this, [this](const QRectF &inner) {
        if (m_activeEditPage < 0) return;
        const QLabel *lbl = m_pageLabels.value(m_activeEditPage, nullptr);
        if (!lbl) return;
        const qreal scale = PdfRenderer::screenScale(m_zoom);
        m_activeEditBounds = QRectF(
            (inner.topLeft() - QPointF(lbl->pos())) / scale,
            inner.size() / scale);
        qWarning() << "[BOUNDS] activeEditBounds=" << m_activeEditBounds;
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
#ifdef HAVE_QT_PDF
    m_liveTimer->stop();
    commitCurrentEdit(m_editorFrame->currentText());
#else
    cancelCurrentEdit();
#endif
    m_editMode = on;
    if (!on) setTool(m_tool); // restore tool cursor when leaving edit mode
}

bool DocumentView::saveToFile(const QString &path)
{
#ifdef HAVE_QT_PDF
    m_liveTimer->stop();
    commitCurrentEdit(m_editorFrame->currentText());
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

QList<QString> DocumentView::allPageTexts() const
{
    QList<QString> result;
#if defined(HAVE_QT_PDF)
    if (!m_document) return result;
    for (int i = 0; i < m_pageCount; ++i)
        result << m_document->getAllText(i).text();
#endif
    return result;
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
    {
        const QRectF px(pdfBoundsPts.topLeft() * scale * dpr,
                        pdfBoundsPts.size()    * scale * dpr);
        // 2 px padding covers anti-aliased glyph edges without reaching
        // adjacent lines at standard leading (gap ≥ 2–3 pt at 100 % zoom).
        const QRect eraseRect = px.adjusted(-2, -2, 2, 2).toAlignedRect();
        QPainter p(&img);
        p.fillRect(eraseRect, Qt::white);
    }
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

// Sample the most common non-white pixel color in a region of a rendered page image.
// Returns a fallback near-black if nothing non-white is found.
static QColor sampleDominantColor(const QImage &img, const QRect &region)
{
    const QRect sr = region.intersected(img.rect());
    if (sr.isEmpty()) return QColor(0x11, 0x11, 0x11);

    QMap<QRgb, int> counts;
    const int stepX = qMax(1, sr.width()  / 60);
    const int stepY = qMax(1, sr.height() / 30);
    for (int y = sr.top(); y < sr.bottom(); y += stepY) {
        for (int x = sr.left(); x < sr.right(); x += stepX) {
            const QColor c = QColor::fromRgb(img.pixel(x, y));
            if (c.red() < 230 || c.green() < 230 || c.blue() < 230)
                counts[c.rgb()]++;
        }
    }
    if (counts.isEmpty()) return QColor(0x11, 0x11, 0x11);

    QRgb best = 0; int bestN = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() > bestN) { bestN = it.value(); best = it.key(); }
    return QColor::fromRgb(best);
}

void DocumentView::handleEditClick(const QPoint &canvasPos)
{
#ifdef HAVE_QT_PDF
    qWarning() << "[EDIT] handleEditClick canvasPos=" << canvasPos;
    auto [pageIdx, pageLbl] = pageAtCanvasPos(canvasPos);
    qWarning() << "[EDIT] pageIdx=" << pageIdx;
    if (pageIdx < 0) return;

    // Convert canvas position to PDF-point coordinates
    const qreal   scale     = PdfRenderer::screenScale(m_zoom);
    const QPointF pageLocal = QPointF(canvasPos - pageLbl->pos());
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

    // If no native/OCR text found, check for a floating session edit at this point
    // (e.g., text that was moved from another position into a blank area).
    double sessionFontPt = 0.0;
    QColor sessionColor;
    bool   isSessionEdit = false;
    if (!block.isValid()) {
        QRectF  editBounds;
        QString editText;
        if (m_session->findEditAt(pageIdx, pdfPt, &editBounds, &editText, &sessionFontPt, &sessionColor)) {
            block        = TextBlock{ pageIdx, editBounds, editText };
            isSessionEdit = true;
        } else {
            return;
        }
    }

    // If this click was the release of a press that committed an edit on this
    // same block, don't re-open — that would call rerenderPageWithBlank and
    // erase the text we just committed.
    if (block.page == m_lastCommittedPage &&
        block.pdfBounds.intersects(m_lastCommittedOrigBounds)) {
        m_lastCommittedPage = -1;
        return;
    }
    m_lastCommittedPage = -1;

    // Convert block bounds back to canvas coordinates for the overlay
    const QRectF canvasBounds(
        block.pdfBounds.topLeft() * scale + QPointF(pageLbl->pos()),
        block.pdfBounds.size() * scale
    );

    // If there's already an edit at this location, load the current text + stored color.
    const QString existing     = m_session->editTextAt(block.page, block.pdfBounds);
    const QString displayText  = existing.isNull() ? block.text : existing;
    const QColor  storedColor  = m_session->editColorAt(block.page, block.pdfBounds);

    m_activeEditPage          = block.page;
    m_activeEditBounds        = block.pdfBounds;
    m_activeEditOriginalBounds = block.pdfBounds;
    m_activeEditOriginalText  = displayText;
    m_hasLiveEdit             = false;

    // Font size: use stored value for session edits; derive from block height otherwise.
    if (isSessionEdit && sessionFontPt > 0.0) {
        m_currentEditorFontSizePt = qMax(4, int(sessionFontPt));
    } else {
        const int lineCount = qMax(1, block.text.count(u'\n') + 1);
        m_currentEditorFontSizePt = qMax(4, int(block.pdfBounds.height() / lineCount * 0.78));
    }
    Q_EMIT editorFontSizeChanged(m_currentEditorFontSizePt);

    // Text color: use stored session color on re-edits; otherwise sample the
    // rendered page.  Render at 1 pt=1 px so coordinates are unambiguous (no
    // device-pixel-ratio complications with the label's pixmap).
    if (storedColor.isValid()) {
        m_currentEditorColor = storedColor;
    } else if (isSessionEdit && sessionColor.isValid()) {
        m_currentEditorColor = sessionColor;
    } else {
        const QImage sampImg = m_renderer->renderPage(block.page, 1.0);
        if (!sampImg.isNull()) {
            const QRect sr = block.pdfBounds.toAlignedRect().intersected(sampImg.rect());
            m_currentEditorColor = sampleDominantColor(sampImg, sr);
            qWarning() << "[COLOR] pdfBounds=" << block.pdfBounds
                       << "sr=" << sr << "imgSize=" << sampImg.size()
                       << "detected=" << m_currentEditorColor;
        }
    }

    const int fontSize = qMax(6, qRound(m_currentEditorFontSizePt * scale));

    m_editorFrame->setDecorations(true);
    m_editorFrame->setForbiddenZones({});
    m_editorFrame->setPageRect(pageLbl->geometry());
    m_editorFrame->resetCommitGuard();
    m_editorFrame->present(displayText, canvasBounds, fontSize, m_currentEditorColor);
    // Erase original text from page render so the editor isn't floating over it.
    rerenderPageWithBlank(block.page, block.pdfBounds);
#endif
}

void DocumentView::commitCurrentEdit(const QString &newText)
{
#ifdef HAVE_QT_PDF
    if (m_activeEditPage < 0) return;
    m_liveTimer->stop();

    // Capture all state before hide() — hide() can trigger a recursive
    // focusOut→committed→commitCurrentEdit call, which exits early because
    // m_activeEditPage is already -1.
    const int    page       = m_activeEditPage;
    const QRectF bounds     = m_activeEditBounds;
    const QRectF origBounds = m_activeEditOriginalBounds;
    const QRectF liveBounds = m_lastLiveEditBounds;
    const bool   hadLive    = m_hasLiveEdit;
    m_activeEditPage = -1;
    m_hasLiveEdit    = false;

    m_editorFrame->hide();  // may trigger recursive commit, which exits early ↑

    qWarning() << "[COMMIT] page=" << page << "new=" << newText.left(40);

    const QString trimNew = newText.trimmed();

    // Remove the original edit (if re-editing previously committed text) and
    // the last live-update edit (tracked precisely to avoid touching other boxes).
    m_session->removeEdit(page, origBounds);
    if (hadLive) m_session->removeEdit(page, liveBounds);

    if (!trimNew.isEmpty())
        m_session->addEdit(page, bounds, newText, m_currentEditorFontSizePt, m_currentEditorColor);

    // Always blank origBounds if:
    //   • the box was moved/shrunk (origBounds no longer covered by new bounds), OR
    //   • the user deleted all text (nothing to paint over the original).
    const bool needBlank = !bounds.contains(origBounds) || trimNew.isEmpty();
    if (needBlank) {
        m_session->addEdit(page, origBounds, QString(), m_currentEditorFontSizePt);
        qWarning() << "[COMMIT] blank added origBounds=" << origBounds
                   << "bounds=" << bounds << "textEmpty=" << trimNew.isEmpty();
    }

    // Remember the original PDF block bounds so that the mouseRelease handler
    // for THIS SAME CLICK can avoid re-opening an editor for the block we just
    // committed — doing so would call rerenderPageWithBlank and blank the text.
    m_lastCommittedPage       = page;
    m_lastCommittedOrigBounds = origBounds;

    // When blanking is needed, rerenderPageWithBlank ensures origBounds is
    // cleared via BOTH applyToImage (session blank edit) and the explicit fill —
    // belt-and-suspenders so the original text definitely disappears.
    if (needBlank)
        rerenderPageWithBlank(page, origBounds);
    else
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
    m_liveTimer->stop();
    if (m_hasLiveEdit) {
        if (page >= 0) m_session->removeEdit(page, m_lastLiveEditBounds);
        m_hasLiveEdit = false;
    }
    m_editorFrame->hide();
    if (page >= 0)
        rerenderPage(page);
#else
    m_activeEditPage = -1;
#endif
}

void DocumentView::liveUpdateCurrentEdit(const QString &text)
{
#ifdef HAVE_QT_PDF
    if (m_activeEditPage < 0) return;
    qWarning() << "[LIVE] page=" << m_activeEditPage
               << "orig=" << m_activeEditOriginalBounds
               << "active=" << m_activeEditBounds
               << "hasLive=" << m_hasLiveEdit
               << "lastLive=" << m_lastLiveEditBounds
               << "text=" << text.left(20);
    // Remove the PREVIOUS live edit using the exact bounds it was placed at.
    // Never use m_activeEditBounds here — the box may have moved on top of
    // another box's edit, and we must not touch that other box.
    if (m_hasLiveEdit) {
        m_session->removeEdit(m_activeEditPage, m_lastLiveEditBounds);
        m_hasLiveEdit = false;
    }
    // Remove the original edit at its original position (handles re-editing
    // previously committed text and the case where the box is being moved).
    m_session->removeEdit(m_activeEditPage, m_activeEditOriginalBounds);

    if (!text.trimmed().isEmpty()) {
        m_session->addEdit(m_activeEditPage, m_activeEditBounds, text,
                           m_currentEditorFontSizePt, m_currentEditorColor);
        m_lastLiveEditBounds = m_activeEditBounds;
        m_hasLiveEdit        = true;
    }
#else
    Q_UNUSED(text)
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
    m_activeEditPage          = pageIdx;
    m_activeEditBounds        = QRectF(
        (QPointF(canvasRect.topLeft()) - QPointF(pageLbl->pos())) / scale,
        QSizeF(canvasRect.size()) / scale);
    m_activeEditOriginalBounds = m_activeEditBounds;
    m_activeEditOriginalText  = QString();
    m_hasLiveEdit             = false;

    m_currentEditorColor = QColor(0x11, 0x11, 0x11);  // new box: no original to sample
    const int fontSize = qMax(8, qRound(12.0 * scale));
    m_editorFrame->setDecorations(true);  // new text box: show border + handles
    m_editorFrame->setPageRect(pageLbl->geometry());
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
#ifdef HAVE_QT_PDF
                // Let TextBoxFrame/InlineEditor handle clicks inside the active frame.
                if (m_editorFrame->isVisible() && m_editorFrame->geometry().contains(cvsPos))
                    return QScrollArea::eventFilter(obj, e);
                // Commit the active edit before starting a new one.
                // cancelCurrentEdit must NOT be used here — it removes the live edit,
                // so the user's typed text would disappear.
                m_liveTimer->stop();
                commitCurrentEdit(m_editorFrame->currentText());
#endif
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
