#include "DocumentView.hpp"
#include "ui/tools/TextAnnotation.hpp"
#include "ui/tools/CommentAnnotation.hpp"
#include "ui/tools/ImageAnnotation.hpp"
#include "ui/tools/UndoCommands.hpp"

#include <QVBoxLayout>
#include <QLabel>
#include <QRubberBand>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QScrollBar>
#include <QFileDialog>
#include <QGuiApplication>
#include <QClipboard>
#include <QMouseEvent>
#include <QFrame>

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
    m_document = new QPdfDocument(this);
#endif
}

DocumentView::~DocumentView() = default;

// ── File loading ──────────────────────────────────────────────────────────────

bool DocumentView::openFile(const QString &path)
{
    if (path.isEmpty()) return false;

#ifdef HAVE_QT_PDF
    auto err = m_document->load(path);
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
    case Tool::Pan:
        viewport()->setCursor(Qt::OpenHandCursor);
        break;
    case Tool::Select:
        viewport()->setCursor(Qt::CrossCursor);
        break;
    default:
        viewport()->setCursor(Qt::ArrowCursor);
        break;
    }
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
    m_undoStack->clear();

    // Delete annotation widgets (not page labels, not drop hint)
    const auto children = m_canvas->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *w : children) {
        if (w == m_dropHint) continue;
        if (m_pageLabels.contains(qobject_cast<QLabel *>(w))) continue;
        delete w;
    }

    // Remove old page labels from layout and delete them
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
        const QSize sz = pageDisplaySize();
        lbl->setFixedSize(sz);
        m_layout->addWidget(lbl, 0, Qt::AlignHCenter);
        m_pageLabels.append(lbl);
    }

    rerenderAll();
}

void DocumentView::rerenderAll()
{
    const QSize sz = pageDisplaySize();
    for (int i = 0; i < m_pageLabels.size(); ++i) {
        m_pageLabels[i]->setFixedSize(sz);
#ifdef HAVE_QT_PDF
        const QImage img = renderPage(i);
        if (!img.isNull())
            m_pageLabels[i]->setPixmap(QPixmap::fromImage(img));
#endif
    }
}

QImage DocumentView::renderPage(int index)
{
#ifdef HAVE_QT_PDF
    const qreal scale = (96.0 / 72.0) * (m_zoom / 100.0);
    const QSizeF pageSize = m_document->pagePointSize(index);
    const QSize pixSize(static_cast<int>(pageSize.width()  * scale),
                        static_cast<int>(pageSize.height() * scale));
    return m_document->render(index, pixSize);
#else
    Q_UNUSED(index)
    return {};
#endif
}

QSize DocumentView::pageDisplaySize() const
{
#ifdef HAVE_QT_PDF
    if (m_document && m_document->pageCount() > 0) {
        const qreal scale = (96.0 / 72.0) * (m_zoom / 100.0);
        const QSizeF s = m_document->pagePointSize(0);
        return QSize(static_cast<int>(s.width() * scale),
                     static_cast<int>(s.height() * scale));
    }
#endif
    const qreal scale = m_zoom / 100.0;
    return QSize(static_cast<int>(595 * scale), static_cast<int>(842 * scale));
}

// ── Event filter (tool handling) ──────────────────────────────────────────────

bool DocumentView::eventFilter(QObject *obj, QEvent *e)
{
    if (obj != viewport()) return QScrollArea::eventFilter(obj, e);

    if (e->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(e);
        if (me->button() == Qt::LeftButton) {
            const QPoint vp     = me->pos();
            const QPoint canvas = vp + QPoint(horizontalScrollBar()->value(),
                                              verticalScrollBar()->value());
            switch (m_tool) {
            case Tool::Select:
                m_selectStart = vp;
                m_rubberBand->setGeometry(QRect(vp, QSize()));
                m_rubberBand->show();
                break;
            case Tool::Pan:
                m_panStart        = vp;
                m_panScrollOrigin = QPoint(horizontalScrollBar()->value(),
                                          verticalScrollBar()->value());
                viewport()->setCursor(Qt::ClosedHandCursor);
                break;
            case Tool::Text:
                placeTextAnnotation(canvas);
                break;
            case Tool::Comment:
                placeCommentAnnotation(canvas);
                break;
            case Tool::Image:
                placeImageAnnotation(canvas);
                break;
            }
        }
    } else if (e->type() == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(e);
        switch (m_tool) {
        case Tool::Select:
            if (me->buttons() & Qt::LeftButton)
                m_rubberBand->setGeometry(QRect(m_selectStart, me->pos()).normalized());
            break;
        case Tool::Pan:
            if (me->buttons() & Qt::LeftButton) {
                const QPoint delta = me->pos() - m_panStart;
                horizontalScrollBar()->setValue(m_panScrollOrigin.x() - delta.x());
                verticalScrollBar()->setValue(m_panScrollOrigin.y() - delta.y());
            }
            break;
        default:
            break;
        }
    } else if (e->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(e);
        if (me->button() == Qt::LeftButton) {
            switch (m_tool) {
            case Tool::Select:
                if (m_rubberBand->isVisible()) {
                    copySelectionToClipboard(m_rubberBand->geometry());
                    m_rubberBand->hide();
                }
                break;
            case Tool::Pan:
                viewport()->setCursor(Qt::OpenHandCursor);
                break;
            default:
                break;
            }
        }
    }

    return QScrollArea::eventFilter(obj, e);
}

// ── Drag & Drop ───────────────────────────────────────────────────────────────

void DocumentView::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls()) {
        for (const QUrl &url : e->mimeData()->urls()) {
            if (url.toLocalFile().endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
                e->acceptProposedAction();
                return;
            }
        }
    }
    e->ignore();
}

void DocumentView::dragMoveEvent(QDragMoveEvent *e)
{
    e->acceptProposedAction();
}

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

// ── Annotations ───────────────────────────────────────────────────────────────

void DocumentView::copySelectionToClipboard(const QRect &viewportRect)
{
    if (viewportRect.isEmpty()) return;
    const QRect canvasRect(
        viewportRect.topLeft() + QPoint(horizontalScrollBar()->value(),
                                        verticalScrollBar()->value()),
        viewportRect.size());
    const QPixmap grab = m_canvas->grab(canvasRect);
    QGuiApplication::clipboard()->setPixmap(grab);
}

void DocumentView::placeTextAnnotation(const QPoint &canvasPos)
{
    auto *ann = new TextAnnotation(m_canvas);
    ann->move(canvasPos);
    ann->show();
    ann->raise();
    m_undoStack->push(new PlaceAnnotationCommand(ann, tr("Add Text")));
    connect(ann, &TextAnnotation::deleteRequested, this, [this, ann]() {
        m_undoStack->push(new DeleteAnnotationCommand(ann, tr("Delete Text")));
    });
    connect(ann, &TextAnnotation::moved, this, [this, ann](const QPoint &from, const QPoint &to) {
        m_undoStack->push(new MoveAnnotationCommand(ann, from, to, tr("Move Text")));
    });
}

void DocumentView::placeCommentAnnotation(const QPoint &canvasPos)
{
    auto *ann = new CommentAnnotation(m_canvas);
    ann->move(canvasPos);
    ann->show();
    ann->raise();
    m_undoStack->push(new PlaceAnnotationCommand(ann, tr("Add Comment")));
    connect(ann, &CommentAnnotation::deleteRequested, this, [this, ann]() {
        m_undoStack->push(new DeleteAnnotationCommand(ann, tr("Delete Comment")));
    });
    connect(ann, &CommentAnnotation::moved, this, [this, ann](const QPoint &from, const QPoint &to) {
        m_undoStack->push(new MoveAnnotationCommand(ann, from, to, tr("Move Comment")));
    });
}

void DocumentView::placeImageAnnotation(const QPoint &canvasPos)
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Image"), {},
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));
    if (path.isEmpty()) return;

    auto *ann = new ImageAnnotation(path, m_canvas);
    ann->move(canvasPos);
    ann->show();
    ann->raise();
    m_undoStack->push(new PlaceAnnotationCommand(ann, tr("Add Image")));
    connect(ann, &ImageAnnotation::deleteRequested, this, [this, ann]() {
        m_undoStack->push(new DeleteAnnotationCommand(ann, tr("Delete Image")));
    });
    connect(ann, &ImageAnnotation::moved, this, [this, ann](const QPoint &from, const QPoint &to) {
        m_undoStack->push(new MoveAnnotationCommand(ann, from, to, tr("Move Image")));
    });
}
