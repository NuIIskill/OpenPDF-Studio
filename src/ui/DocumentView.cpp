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

    m_dropHint = new QLabel(tr("Drop a PDF here or click a tab to open"), m_canvas);
    m_dropHint->setObjectName(QStringLiteral("DropHint"));
    m_dropHint->setAlignment(Qt::AlignCenter);
    m_dropHint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_layout->addWidget(m_dropHint);

    setWidget(m_canvas);
    viewport()->installEventFilter(this);
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());

#ifdef HAVE_QT_PDF
    m_document  = new QPdfDocument(this);
    m_renderer  = new PdfRenderer(m_document);
    m_extractor = new PdfTextExtractor(m_document);
    m_session   = new EditSession();

    m_editor = new InlineEditor(m_canvas);
    m_editor->hide();
    connect(m_editor, &InlineEditor::committed, this, &DocumentView::commitCurrentEdit);
    connect(m_editor, &InlineEditor::cancelled,  this, &DocumentView::cancelCurrentEdit);
#endif
}

DocumentView::~DocumentView()
{
#ifdef HAVE_QT_PDF
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
#else
    m_filePath  = path;
    m_pageCount = 1;
#endif

    buildPages();
    Q_EMIT fileOpened(m_filePath, m_pageCount);
    Q_EMIT pageChanged(1, m_pageCount);
    return true;
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
    if (m_editMode) return;
    switch (tool) {
    case Tool::Pan:    viewport()->setCursor(Qt::OpenHandCursor); break;
    case Tool::Select: viewport()->setCursor(Qt::CrossCursor);    break;
    default:           viewport()->setCursor(Qt::ArrowCursor);    break;
    }
}

void DocumentView::setEditMode(bool on)
{
    if (m_editMode == on) return;
    cancelCurrentEdit();
    m_editMode = on;
    viewport()->setCursor(on ? Qt::IBeamCursor : Qt::ArrowCursor);
}

bool DocumentView::saveToFile(const QString &path)
{
#ifdef HAVE_QT_PDF
    cancelCurrentEdit();
    return m_session->saveToFile(path, m_document, m_pageCount);
#else
    Q_UNUSED(path)
    return false;
#endif
}

void DocumentView::retranslateUi()
{
    m_dropHint->setText(tr("Drop a PDF here or click a tab to open"));
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
#ifdef HAVE_QT_PDF
    for (int i = 0; i < m_pageLabels.size(); ++i) {
        const QSize sz = m_renderer->pageDisplaySize(i, m_zoom);
        m_pageLabels[i]->setFixedSize(sz);
        const qreal scale = PdfRenderer::screenScale(m_zoom);
        QImage img = m_renderer->renderPage(i, scale);
        m_session->applyToImage(i, img, scale);
        if (!img.isNull())
            m_pageLabels[i]->setPixmap(QPixmap::fromImage(img));
    }
#endif
}

void DocumentView::rerenderPage(int page)
{
#ifdef HAVE_QT_PDF
    if (page < 0 || page >= m_pageLabels.size()) return;
    const qreal scale = PdfRenderer::screenScale(m_zoom);
    QImage img = m_renderer->renderPage(page, scale);
    m_session->applyToImage(page, img, scale);
    if (!img.isNull())
        m_pageLabels[page]->setPixmap(QPixmap::fromImage(img));
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
    auto [pageIdx, pageLbl] = pageAtCanvasPos(canvasPos);
    if (pageIdx < 0) return;

    // Convert canvas position to PDF-point view coordinates
    const QPointF pageLocal = QPointF(canvasPos - pageLbl->pos());
    const qreal   scale     = PdfRenderer::screenScale(m_zoom);
    const QPointF pdfPt     = pageLocal / scale;
    const QSizeF  pageSize  = m_renderer->pageSizePts(pageIdx);

    const TextBlock block = m_extractor->textAt(pageIdx, pdfPt, pageSize);
    if (!block.isValid()) return;

    // Convert block bounds back to canvas coordinates for the overlay
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
#endif
}

void DocumentView::commitCurrentEdit(const QString &newText)
{
#ifdef HAVE_QT_PDF
    m_editor->hide();

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
#ifdef HAVE_QT_PDF
    m_editor->hide();
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
            if (m_editMode) {
#ifdef HAVE_QT_PDF
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
