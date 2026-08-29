

#include "ui/DocumentView.hpp"

#include "app/PdfPwStore.hpp"
#include "engine/document/DocumentSource.hpp"
#include "engine/edit/InkMetrics.hpp"
#include "app/SafeWrite.hpp"
#include "app/SessionStore.hpp"
#include "ui/tools/ImageAnnotation.hpp"
#include "ui/view/ImageAnnotationLayer.hpp"
#include "ui/view/LinkAnnotationLayer.hpp"
#include "ui/notes/NoteLayer.hpp"
#include "ui/draw/DrawingLayer.hpp"
#include "ui/view/HoverHighlight.hpp"
#include "ui/view/FindController.hpp"
#include "ui/view/PageLayoutEngine.hpp"
#include "ui/view/PageOverlay.hpp"
#include "ui/view/ZoomController.hpp"
#include "ui/view/TextSelectionController.hpp"
#include "ui/widgets/PasswordDialog.hpp"

#include <QFileInfo>

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <cstring>
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

    m_gridCanvas = new QWidget();
    m_gridCanvas->setObjectName(QStringLiteral("GridCanvas"));

    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this]() { reportCurrentPage(); syncVisibleRect(); });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this]() { syncVisibleRect(); });

    viewport()->installEventFilter(this);
    m_canvas->installEventFilter(this);
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());
    retranslateUi();

    m_selection = new TextSelectionController(this, this);
    connect(m_selection, &TextSelectionController::focusRequested,
            this, [this]() { setFocus(Qt::MouseFocusReason); });

    m_find = new FindController(this, viewport(), m_src.get(), this);
    connect(m_find, &FindController::matchActivated,
            this, &DocumentView::scrollToSearchMatch);

    m_imageLayer = new ImageAnnotationLayer(this, this);
    connect(m_imageLayer, &ImageAnnotationLayer::pageNeedsRerender,
            this, &DocumentView::rerenderPage);
    connect(m_imageLayer, &ImageAnnotationLayer::imageAdded, this, [this](int page) {
        m_journal.recordChange({ DocumentHistory::Kind::ImageInserted, page });
    });
    connect(m_imageLayer, &ImageAnnotationLayer::imageRemoved, this, [this](int page) {
        m_journal.recordChange({ DocumentHistory::Kind::ImageRemoved, page });
    });
    m_linkLayer = new LinkAnnotationLayer(this, this);
    connect(m_linkLayer, &LinkAnnotationLayer::pageNeedsRerender,
            this, &DocumentView::rerenderPage);
    connect(m_linkLayer, &LinkAnnotationLayer::linkAdded,
            this, [this](int page, int count) {
        m_journal.recordChange({ DocumentHistory::Kind::LinkAdded, page, count });
    });
    connect(m_linkLayer, &LinkAnnotationLayer::linkEdited, this, [this](int page) {
        m_journal.recordChange({ DocumentHistory::Kind::LinkEdited, page });
    });
    connect(m_linkLayer, &LinkAnnotationLayer::linkRemoved, this, [this](int page) {
        m_journal.recordChange({ DocumentHistory::Kind::LinkRemoved, page });
    });
    m_noteLayer = new NoteLayer(this, this);
    connect(m_noteLayer, &NoteLayer::notesChanged,
            this, &DocumentView::notesChanged);
    connect(m_noteLayer, &NoteLayer::noteActivated, this,
            [this](const QString &id, int page) {
        goToPage(page);
        Q_EMIT noteSelected(id);
    });
    connect(m_noteLayer, &NoteLayer::noteAdded, this, [this](int page) {
        m_journal.recordChange({ DocumentHistory::Kind::NoteAdded, page });
    });
    connect(m_noteLayer, &NoteLayer::noteEdited, this, [this](int page) {
        m_journal.recordChange({ DocumentHistory::Kind::NoteEdited, page });
    });
    connect(m_noteLayer, &NoteLayer::noteRemoved, this, [this](int page) {
        m_journal.recordChange({ DocumentHistory::Kind::NoteRemoved, page });
    });
    m_drawingLayer = new DrawingLayer(this, this);
    connect(m_drawingLayer, &DrawingLayer::pageNeedsRerender,
            this, &DocumentView::rerenderPage);
    connect(m_drawingLayer, &DrawingLayer::strokeAdded, this, [this](int page) {
        m_journal.recordChange({ DocumentHistory::Kind::DrawingAdded, page });
    });
    connect(m_drawingLayer, &DrawingLayer::strokesRemoved, this, [this](int page) {
        m_journal.recordChange({ DocumentHistory::Kind::DrawingRemoved, page });
    });

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

    m_overlays = PageOverlays::createAll(this, this);

    connect(m_layoutEngine, &PageLayoutEngine::layoutChanged,
            this, &DocumentView::repositionPageOverlays);
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
    connect(m_editorFrame, &TextBoxFrame::changed, this, [this]() {
        m_edit.refreshLivePreview();
    });
    connect(&m_edit, &EditController::livePreviewChanged, this,
            [this](int page, const QList<EditSession::Edit> &edits) {
        m_layoutEngine->setPreviewEdits(page, edits);
    });

    connect(m_editorFrame, &TextBoxFrame::dragEnded, this, [this]() {

        if (m_edit.activeEditSourcePage >= 0 && m_edit.activeEditNeedsBlank)
            rerenderPageWithBlank(m_edit.activeEditSourcePage, m_edit.activeEditOriginalBounds);
    });
    connect(m_editorFrame, &TextBoxFrame::boundsChanged, this, [this](const QRectF &inner) {
        if (m_edit.activeEditPage < 0) return;

        auto [pg, lbl] = pageAtCanvasPos(inner.center().toPoint());
        if (pg < 0 || !lbl) {
            pg  = m_edit.activeEditPage;
            lbl = pageLabel(pg);
            if (!lbl) return;
        }
        if (pg != m_edit.activeEditPage) {
            m_edit.activeEditPage = pg;

            m_editorFrame->setPageRect(lbl->geometry());
        }
        const qreal scale = PdfRenderer::screenScale(m_zoomCtl->zoom());
        QRectF newBounds(
            (inner.topLeft() - QPointF(lbl->pos())) / scale,
            inner.size() / scale);
        m_edit.clampToPdfPage(pg, newBounds);
        m_edit.activeEditBounds = newBounds;
        m_edit.currentBox.bounds = newBounds;
        m_edit.notifyBoundsChanged();
        m_edit.refreshLivePreview();
    });
    m_hover->setEditorFrame(m_editorFrame);

    m_selection->setSource(m_src->renderer(), m_src.get());
    m_layoutEngine->setSource(m_src->renderer(), m_session);
    m_linkLayer->setSource(m_src->backend(), m_session, m_undoStack);
    m_noteLayer->setSource(m_src->backend(), m_session, m_undoStack);
    m_drawingLayer->setSource(m_session, m_undoStack);
#endif

    m_ocrEngine = new OcrEngine();
#ifdef HAVE_PDF_RENDERING

    m_imageLayer->setSource(m_src->renderer(), m_session, m_ocrEngine, m_src->contentPath());
#endif

    m_edit.attach(this, m_src.get(), m_editorFrame, m_zoomCtl, m_hover,
                  m_ocrEngine, m_undoStack);

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
    connect(&m_edit, &EditController::textBoxPropertiesChanged,
            this, &DocumentView::textBoxPropertiesChanged);
    connect(&m_edit, &EditController::textBoxEditingChanged,
            this, &DocumentView::textBoxEditingChanged);
    connect(&m_edit, &EditController::pageNeedsRerender,
            this, &DocumentView::rerenderPage);
    connect(&m_edit, &EditController::pageNeedsBlank,
            this, &DocumentView::rerenderPageWithBlank);

    connect(&m_edit, &EditController::changeRecorded, this,
            [this](const DocumentHistory::Change &c) { m_journal.recordChange(c); });
}

DocumentView::~DocumentView()
{

    m_undoStack->disconnect(this);

    SessionStore::discard(m_src->contentPath());
    delete m_ocrEngine;
#ifdef HAVE_PDF_RENDERING

    delete m_session;
#endif
}

void DocumentView::setEditMode(bool on)
{
    if (m_editMode == on) return;
#ifdef HAVE_PDF_RENDERING
    commitCurrentEdit(m_editorFrame->currentText());
#endif
    m_hover->hide();
    m_editMode = on;
    if (!on) setTool(m_tool);
}

QRectF DocumentView::editBounds() const
{
#ifdef HAVE_PDF_RENDERING
    return m_edit.activeEditPage >= 0 ? m_edit.activeEditBounds : QRectF();
#else
    return {};
#endif
}

double DocumentView::editFontSizePt() const
{
#ifdef HAVE_PDF_RENDERING
    return m_edit.activeEditPage >= 0 ? m_edit.currentEditorFontSizePt : 0.0;
#else
    return 0.0;
#endif
}

QRectF DocumentView::editFrameRect() const
{
#ifdef HAVE_PDF_RENDERING
    if (m_edit.activeEditPage < 0 || !m_editorFrame->isVisible()) return {};
    const QLabel *lbl = pageLabel(m_edit.activeEditPage);
    if (!lbl) return {};
    const qreal scale = PdfRenderer::screenScale(m_zoomCtl->zoom());
    const QRectF inner = m_editorFrame->innerCanvasRect();
    return QRectF((inner.topLeft() - QPointF(lbl->pos())) / scale,
                  inner.size() / scale);
#else
    return {};
#endif
}

void DocumentView::rerenderPageWithBlank(int page, const QRectF &pdfBoundsPts)
{
#ifdef HAVE_PDF_RENDERING
    m_layoutEngine->rerenderPageWithBlank(page, pdfBoundsPts, m_edit.activeEditEraseRects);
#else
    Q_UNUSED(page) Q_UNUSED(pdfBoundsPts)
#endif
}

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

    m_imageLayer->clear();
    m_noteLayer->clear();
    m_drawingLayer->clear();
    m_undoStack->clear();
    m_journal.savedImageRevision = m_session->imageRevision();
}

#endif

void DocumentView::undo()
{
    closeEditorBeforeUndo();
    m_undoStack->undo();
}

void DocumentView::redo()
{
    closeEditorBeforeUndo();
    m_undoStack->redo();
}

void DocumentView::closeEditorBeforeUndo()
{
#ifdef HAVE_PDF_RENDERING
    commitCurrentEdit(m_editorFrame->currentText());
#endif
}

void DocumentView::keepScroll(const std::function<void()> &schliessen)
{
    const int x = horizontalScrollBar()->value();
    const int y = verticalScrollBar()->value();
    schliessen();
    horizontalScrollBar()->setValue(x);
    verticalScrollBar()->setValue(y);

    QTimer::singleShot(0, this, [this, x, y]() {
        horizontalScrollBar()->setValue(x);
        verticalScrollBar()->setValue(y);
    });
}

void DocumentView::commitCurrentEdit(const QString &newText)
{
#ifdef HAVE_PDF_RENDERING
    keepScroll([this, &newText] { m_edit.commit(newText); });
#else
    Q_UNUSED(newText)
#endif
}

void DocumentView::cancelCurrentEdit()
{
#ifdef HAVE_PDF_RENDERING
    keepScroll([this] { m_edit.cancel(); });
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

void DocumentView::refreshEditorFontLive()                     { m_edit.refreshFontLive(); }
void DocumentView::setEditorFontFamily(const QString &family)  { m_edit.setFontFamily(family); }
void DocumentView::setEditorBold(bool on)                      { m_edit.setBold(on); }
void DocumentView::setEditorItalic(bool on)                    { m_edit.setItalic(on); }
void DocumentView::setEditorUnderline(bool on)                 { m_edit.setUnderline(on); }
void DocumentView::setEditorFontSize(int ptSize)               { m_edit.setFontSize(ptSize); }
void DocumentView::setEditorTextColor(const QColor &color)     { m_edit.setTextColor(color); }
void DocumentView::setTextBoxProperties(const TextBoxProperties &p) { m_edit.setTextBoxProperties(p); }
void DocumentView::setTextBoxDefaults(const TextBoxProperties &p) { m_edit.setTextBoxDefaults(p); }
void DocumentView::setEditorAlignment(Qt::Alignment a) { m_edit.setHorizontalAlignment(a); }
void DocumentView::setEditorListStyle(TextBoxProperties::ListStyle s) { m_edit.setListStyle(s); }
void DocumentView::changeEditorIndent(int delta) { m_edit.changeIndent(delta); }
void DocumentView::setEditorLineSpacing(double multiplier) { m_edit.setLineSpacing(multiplier); }
