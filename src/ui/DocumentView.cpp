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

    m_dropHint = new QLabel(m_canvas);
    m_dropHint->setObjectName(QStringLiteral("DropHint"));
    m_dropHint->setAlignment(Qt::AlignCenter);
    m_dropHint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_layout->addWidget(m_dropHint);

    setWidget(m_canvas);
    viewport()->installEventFilter(this);
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());

#ifdef HAVE_PDF_RENDERING
    m_session     = new EditSession();
    m_editor      = new InlineEditor(m_canvas);
    m_editor->hide();
    connect(m_editor, &InlineEditor::committed, this, &DocumentView::commitCurrentEdit);
    connect(m_editor, &InlineEditor::cancelled,  this, &DocumentView::cancelCurrentEdit);
    m_editorFrame = new TextBoxFrame(m_canvas);

#  ifdef HAVE_QT_PDF
    m_document  = new QPdfDocument(this);
    m_renderer  = new PdfRenderer(m_document);
    m_extractor = new PdfTextExtractor(m_document);
#  endif
    // HAVE_POPPLER: m_renderer + m_extractor are created per-file in openFile()
#endif

    retranslateUi();
}

DocumentView::~DocumentView()
{
#ifdef HAVE_PDF_RENDERING
    delete m_renderer;
    delete m_extractor;
    delete m_session;
#endif
}

// ── File ──────────────────────────────────────────────────────────────────────

void DocumentView::clearDocument()
{
    cancelCurrentEdit();
#ifdef HAVE_QT_PDF
    m_session->clear();
    m_document->close();
#elif defined(HAVE_POPPLER)
    m_session->clear();
    delete m_renderer;  m_renderer  = nullptr;
    delete m_extractor; m_extractor = nullptr;
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
    m_session->clear();
    delete m_renderer;  m_renderer  = new PdfRenderer(doc.get());
    delete m_extractor; m_extractor = new PdfTextExtractor(doc.get());
    m_popplerDoc = std::move(doc);
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
    if (!on) setTool(m_tool);
}

bool DocumentView::saveToFile(const QString &path)
{
    cancelCurrentEdit();
#if defined(HAVE_QT_PDF) && defined(HAVE_QT_PRINT)
    return m_session->saveToFile(path, m_document, m_pageCount);
#elif defined(HAVE_POPPLER) && defined(HAVE_QT_PRINT)
    return m_session->saveToFile(path, m_popplerDoc.get(), m_pageCount);
#else
    Q_UNUSED(path)
    return false;
#endif
}

bool DocumentView::hasUnsavedEdits() const
{
#ifdef HAVE_PDF_RENDERING
    return m_session->hasAnyEdits();
#else
    return false;
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
#  ifdef HAVE_QT_PDF
        m_session->applyToImage(i, img, scale * dpr);
#  else
        m_session->applyToImage(i, img, scale);
#  endif
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
#  ifdef HAVE_QT_PDF
    m_session->applyToImage(page, img, scale * dpr);
#  else
    m_session->applyToImage(page, img, scale);
#  endif
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
#ifdef HAVE_PDF_RENDERING
    if (!m_extractor) return;
    auto [pageIdx, pageLbl] = pageAtCanvasPos(canvasPos);
    if (pageIdx < 0) return;

    const QPointF pageLocal = QPointF(canvasPos - pageLbl->pos());
    const qreal   scale     = PdfRenderer::screenScale(m_zoom);
    const QPointF pdfPt     = pageLocal / scale;
    const QSizeF  pageSize  = m_renderer->pageSizePts(pageIdx);

    const TextBlock block = m_extractor->textAt(pageIdx, pdfPt, pageSize);
    if (!block.isValid()) return;

    const QRectF canvasBounds(
        block.pdfBounds.topLeft() * scale + QPointF(pageLbl->pos()),
        block.pdfBounds.size() * scale
    );

    m_activeEditPage         = block.page;
    m_activeEditBounds       = block.pdfBounds;
    m_activeEditOriginalText = block.text;

    const int fontSize = qMax(8, int(block.pdfBounds.height() * scale * 0.78));
    m_editor->resetCommitGuard();
    m_editor->present(block.text, canvasBounds, fontSize);
    m_editorFrame->presentAround(canvasBounds);
#endif
}

void DocumentView::commitCurrentEdit(const QString &newText)
{
#ifdef HAVE_PDF_RENDERING
    m_editor->hide();
    m_editorFrame->hide();

    if (m_activeEditPage < 0) return;

    const QString trimNew = newText.trimmed();
    const QString trimOld = m_activeEditOriginalText.trimmed();

    if (trimNew != trimOld && !trimNew.isEmpty())
        m_session->addEdit(m_activeEditPage, m_activeEditBounds, newText);

    const int page = m_activeEditPage;
    m_activeEditPage = -1;
    rerenderPage(page);
#else
    Q_UNUSED(newText)
#endif
}

void DocumentView::cancelCurrentEdit()
{
#ifdef HAVE_PDF_RENDERING
    m_editor->hide();
    m_editorFrame->hide();
#endif
    m_activeEditPage = -1;
}

// ── Event filter ──────────────────────────────────────────────────────────────

bool DocumentView::eventFilter(QObject *obj, QEvent *e)
{
    if (obj != viewport()) return QScrollArea::eventFilter(obj, e);

    if (e->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(e);
        if (me->button() == Qt::LeftButton) {
            const QPoint canvas = me->pos() + QPoint(horizontalScrollBar()->value(),
                                                     verticalScrollBar()->value());
            if (m_editMode && m_tool == Tool::Text) {
#ifdef HAVE_PDF_RENDERING
                if (m_editor->isVisible() &&
                    m_editor->geometry().contains(canvas))
                    return QScrollArea::eventFilter(obj, e);
#endif
                cancelCurrentEdit();
                handleEditClick(canvas);
                return true;
            }

            switch (m_tool) {
            case Tool::Select:
                m_selectStart = me->pos();
                m_rubberBand->setGeometry(QRect(me->pos(), QSize()));
                m_rubberBand->show();
                break;
            case Tool::Pan:
                m_panStart        = me->pos();
                m_panScrollOrigin = { horizontalScrollBar()->value(),
                                     verticalScrollBar()->value() };
                viewport()->setCursor(Qt::ClosedHandCursor);
                break;
            default: break;
            }
        }
    } else if (e->type() == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(e);
        if (!m_editMode) {
            switch (m_tool) {
            case Tool::Select:
                if (me->buttons() & Qt::LeftButton)
                    m_rubberBand->setGeometry(QRect(m_selectStart, me->pos()).normalized());
                break;
            case Tool::Pan:
                if (me->buttons() & Qt::LeftButton) {
                    const QPoint d = me->pos() - m_panStart;
                    horizontalScrollBar()->setValue(m_panScrollOrigin.x() - d.x());
                    verticalScrollBar()->setValue(m_panScrollOrigin.y() - d.y());
                }
                break;
            default: break;
            }
        }
    } else if (e->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(e);
        if (me->button() == Qt::LeftButton && !m_editMode) {
            switch (m_tool) {
            case Tool::Select: m_rubberBand->hide();                        break;
            case Tool::Pan:    viewport()->setCursor(Qt::OpenHandCursor);   break;
            default: break;
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
