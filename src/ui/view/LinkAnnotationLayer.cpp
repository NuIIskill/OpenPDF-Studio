#include "ui/view/LinkAnnotationLayer.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/PdfBackend.hpp"
#  include "engine/edit/EditSession.hpp"
#endif

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFrame>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUrl>

#include <utility>

class LinkUndoCommand : public QUndoCommand
{
public:
    LinkUndoCommand(LinkAnnotationLayer *layer,
                    QList<LinkAnnotationLayer::State> before,
                    QList<LinkAnnotationLayer::State> after,
                    const QString &text)
        : QUndoCommand(text)
        , m_layer(layer)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {}

    void undo() override { m_layer->restoreState(m_before); }
    void redo() override
    {
        if (m_firstRedo) { m_firstRedo = false; return; }
        m_layer->restoreState(m_after);
    }

private:
    LinkAnnotationLayer *m_layer { nullptr };
    QList<LinkAnnotationLayer::State> m_before;
    QList<LinkAnnotationLayer::State> m_after;
    bool m_firstRedo { true };
};

LinkAnnotationLayer::LinkAnnotationLayer(PageCanvas *canvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
{}

#ifdef HAVE_PDF_RENDERING
void LinkAnnotationLayer::setSource(PdfBackend *backend, EditSession *session,
                                    QUndoStack *undo)
{
    m_backend = backend;
    m_session = session;
    m_undo    = undo;
}
#endif

void LinkAnnotationLayer::reload()
{
    clear();
#ifdef HAVE_PDF_RENDERING
    if (!m_backend) return;
    for (int page = 0; page < m_canvas->pageCount(); ++page) {
        for (const PdfBackend::Link &link : m_backend->pageLinks(page)) {
            Entry entry;
            entry.page           = page;
            entry.originalBounds = link.bounds;
            entry.bounds         = link.bounds;
            entry.originalUrl    = link.url;
            entry.url            = link.url;
            entry.textRects      = link.textRects;
            entry.existing       = true;
            entry.colorText      = link.styledByOpenPdf;
            addEntry(std::move(entry), false);
        }
    }
    relayout();
#endif
}

void LinkAnnotationLayer::clear()
{
    m_pressedLink = nullptr;
    for (const Entry &entry : std::as_const(m_entries))
        delete entry.widget;
    m_entries.clear();
}

void LinkAnnotationLayer::setToolActive(bool active)
{
    Q_UNUSED(active)
    for (const Entry &entry : std::as_const(m_entries))
        if (entry.widget) entry.widget->setVisible(!entry.removed);
}

void LinkAnnotationLayer::addEntry(Entry entry, bool position)
{
    auto *frame = new QFrame(m_canvas->canvasWidget());
    frame->setObjectName(QStringLiteral("LinkAnnotation"));
    frame->setCursor(Qt::PointingHandCursor);
    frame->setToolTip(entry.url);
    frame->installEventFilter(this);
    frame->setContextMenuPolicy(Qt::CustomContextMenu);
    frame->setStyleSheet(QStringLiteral(
        "QFrame#LinkAnnotation {"
        " background: transparent;"
        " border: none;"
        "}"));
    connect(frame, &QWidget::customContextMenuRequested, this,
            [this, frame](const QPoint &pos) {
        showContextMenu(frame, frame->mapToGlobal(pos));
    });
    const bool removed = entry.removed;
    entry.widget = frame;
    m_entries.append(std::move(entry));
    if (position) relayout();
    frame->setVisible(!removed);
}

bool LinkAnnotationLayer::eventFilter(QObject *watched, QEvent *event)
{
    auto *frame = qobject_cast<QFrame *>(watched);
    if (!frame || indexOf(frame) < 0)
        return QObject::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            m_pressedLink = frame;
            m_pressGlobal = mouse->globalPosition().toPoint();
            return true;
        }
    } else if (event->type() == QEvent::MouseMove && m_pressedLink == frame) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if ((mouse->globalPosition().toPoint() - m_pressGlobal).manhattanLength()
                > QApplication::startDragDistance())
            m_pressedLink = nullptr;
        return true;
    } else if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            const bool open = m_pressedLink == frame;
            m_pressedLink = nullptr;
            if (open) {
                const int index = indexOf(frame);
                if (index >= 0)
                    QDesktopServices::openUrl(
                        QUrl::fromUserInput(m_entries.at(index).url));
            }
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

int LinkAnnotationLayer::indexOf(QFrame *widget) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).widget == widget) return i;
    return -1;
}

QString LinkAnnotationLayer::askForUrl(const QString &current)
{
    bool accepted = false;
    const QString shown = current.isEmpty()
        ? QStringLiteral("https://") : QUrl(current).toDisplayString();
    const QString value = QInputDialog::getText(
        m_canvas->canvasWidget(), tr("Link"), tr("Link address:"),
        QLineEdit::Normal, shown, &accepted).trimmed();
    if (!accepted || value.isEmpty()) return {};

    const QUrl url = QUrl::fromUserInput(value);
    if (!url.isValid() || url.scheme().isEmpty()) return {};
    return url.toString(QUrl::FullyEncoded);
}

void LinkAnnotationLayer::showContextMenu(QFrame *widget, const QPoint &globalPos)
{
    const int index = indexOf(widget);
    if (index < 0) return;

    QMenu menu(m_canvas->canvasWidget());
    QAction *open = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")),
                                   tr("Open Link"));
    QAction *newTab = menu.addAction(QIcon::fromTheme(QStringLiteral("tab-new")),
                                     tr("Open Link in New Tab"));
    QAction *copy = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")),
                                   tr("Copy Link Address"));
    menu.addSeparator();
    QAction *edit = menu.addAction(QIcon::fromTheme(QStringLiteral("document-edit")),
                                   tr("Edit Link"));
    QAction *remove = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                                     tr("Delete Link"));
    menu.addSeparator();
    QAction *add = menu.addAction(tr("Add Link..."));

    QAction *chosen = menu.exec(globalPos);
    if (!chosen) return;

    if (chosen == open || chosen == newTab) {
        QDesktopServices::openUrl(QUrl::fromUserInput(m_entries.at(index).url));
        return;
    }
    if (chosen == copy) {
        QApplication::clipboard()->setText(m_entries.at(index).url);
        return;
    }
    if (chosen == edit) {
        const QString url = askForUrl(m_entries.at(index).url);
        if (url.isEmpty()) return;
        const QList<State> before = state();
        const int page = m_entries.at(index).page;
        m_entries[index].url = url;
        widget->setToolTip(url);
        syncSession();
        pushUndo(before, tr("Edit link"));
        Q_EMIT linkEdited(page);
        return;
    }
    if (chosen == remove) {
        const QList<State> before = state();
        const int page = m_entries.at(index).page;
        const bool recolor = m_entries.at(index).colorText;
        if (m_entries.at(index).existing) {
            m_entries[index].removed = true;
            widget->hide();
        } else {
            delete m_entries.at(index).widget;
            m_entries.removeAt(index);
        }
        syncSession();
        pushUndo(before, tr("Delete link"));
        if (recolor) Q_EMIT pageNeedsRerender(page);
        Q_EMIT linkRemoved(page);
        return;
    }
    if (chosen == add)
        addAt(widget->geometry().bottomLeft() + QPoint(0, 8));
}

bool LinkAnnotationLayer::showEmptyContextMenu(
    const QPoint &canvasPos, const QPoint &globalPos,
    const QList<TextSelectionController::SelectionPart> &selection)
{
    QMenu menu(m_canvas->canvasWidget());
    QAction *add = menu.addAction(tr("Add Link..."));
    if (menu.exec(globalPos) != add) return false;
    if (!selection.isEmpty()) return addSelection(selection);
    addAt(canvasPos);
    return false;
}

bool LinkAnnotationLayer::addSelection(
    const QList<TextSelectionController::SelectionPart> &selection)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_session) return false;
    const QString url = askForUrl();
    if (url.isEmpty()) return false;
    const QList<State> before = state();

    QList<int> pages;
    for (const auto &part : selection) {
        if (part.page < 0 || part.rects.isEmpty()) continue;
        QRectF bounds;
        for (const QRectF &rect : part.rects)
            bounds = bounds.isNull() ? rect : bounds.united(rect);
        if (!bounds.isValid()) continue;

        Entry entry;
        entry.page      = part.page;
        entry.bounds    = bounds;
        entry.url       = url;
        entry.textRects = part.rects;
        entry.colorText = true;
        addEntry(std::move(entry));
        if (!pages.contains(part.page)) pages.append(part.page);
    }
    if (pages.isEmpty()) return false;
    syncSession();
    pushUndo(before, tr("Add link"));
    for (int page : std::as_const(pages)) Q_EMIT pageNeedsRerender(page);
    Q_EMIT linkAdded(pages.first(), pages.size());
    return true;
#else
    Q_UNUSED(selection)
    return false;
#endif
}

void LinkAnnotationLayer::addAt(const QPoint &canvasPos)
{
    auto [page, label] = m_canvas->pageAtCanvasPos(canvasPos);
    if (page < 0 || !label) return;
    const QRect pageRect = label->geometry();
    const int width = qMin(180, pageRect.right() - canvasPos.x() + 1);
    const int height = qMin(28, pageRect.bottom() - canvasPos.y() + 1);
    if (width < 20 || height < 10) return;
    addInRect(QRect(canvasPos, QSize(width, height)));
}

void LinkAnnotationLayer::addInRect(const QRect &canvasRect)
{
#ifdef HAVE_PDF_RENDERING
    auto [page, label] = m_canvas->pageAtCanvasPos(canvasRect.center());
    if (page < 0 || !label || !m_session) return;
    const QString url = askForUrl();
    if (url.isEmpty()) return;

    const QRect rect = canvasRect.normalized().intersected(label->geometry());
    if (rect.width() < 10 || rect.height() < 8) return;
    const QList<State> before = state();
    const qreal scale = m_canvas->screenScale();

    Entry entry;
    entry.page   = page;
    entry.bounds = QRectF(QPointF(rect.topLeft() - label->pos()) / scale,
                          QSizeF(rect.size()) / scale);
    entry.url    = url;
    addEntry(std::move(entry));
    syncSession();
    pushUndo(before, tr("Add link"));
    Q_EMIT pageNeedsRerender(page);
    Q_EMIT linkAdded(page, 1);
#else
    Q_UNUSED(canvasRect)
#endif
}

void LinkAnnotationLayer::syncSession()
{
#ifdef HAVE_PDF_RENDERING
    if (!m_session) return;
    QList<EditSession::LinkEdit> edits;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (!entry.existing && entry.removed) continue;
        if (entry.existing && !entry.removed
                && entry.bounds == entry.originalBounds
                && entry.url == entry.originalUrl)
            continue;
        EditSession::LinkEdit edit;
        edit.page           = entry.page;
        edit.originalBounds = entry.originalBounds;
        edit.pdfBounds      = entry.bounds;
        edit.originalUrl    = entry.originalUrl;
        edit.url            = entry.url;
        edit.textRects      = entry.textRects;
        edit.existing       = entry.existing;
        edit.removed        = entry.removed;
        edit.colorText      = entry.colorText;
        edits.append(std::move(edit));
    }
    m_session->replaceLinkEdits(std::move(edits));
#endif
}

QList<LinkAnnotationLayer::State> LinkAnnotationLayer::state() const
{
    QList<State> result;
    result.reserve(m_entries.size());
    for (const Entry &entry : m_entries) {
        State item;
        item.page           = entry.page;
        item.originalBounds = entry.originalBounds;
        item.bounds         = entry.bounds;
        item.originalUrl    = entry.originalUrl;
        item.url            = entry.url;
        item.textRects      = entry.textRects;
        item.existing       = entry.existing;
        item.removed        = entry.removed;
        item.colorText      = entry.colorText;
        result.append(std::move(item));
    }
    return result;
}

void LinkAnnotationLayer::restoreState(const QList<State> &restored)
{
    QList<int> pages;
    for (const Entry &entry : std::as_const(m_entries))
        if (!pages.contains(entry.page)) pages.append(entry.page);

    clear();
    for (const State &item : restored) {
        Entry entry;
        entry.page           = item.page;
        entry.originalBounds = item.originalBounds;
        entry.bounds         = item.bounds;
        entry.originalUrl    = item.originalUrl;
        entry.url            = item.url;
        entry.textRects      = item.textRects;
        entry.existing       = item.existing;
        entry.removed        = item.removed;
        entry.colorText      = item.colorText;
        addEntry(std::move(entry), false);
        if (!pages.contains(item.page)) pages.append(item.page);
    }
    relayout();
    syncSession();
    for (int page : std::as_const(pages))
        if (page >= 0) Q_EMIT pageNeedsRerender(page);
}

void LinkAnnotationLayer::pushUndo(const QList<State> &before, const QString &text)
{
#ifdef HAVE_PDF_RENDERING
    const QList<State> after = state();
    if (!m_undo || before == after) return;
    m_undo->push(new LinkUndoCommand(this, before, after, text));
#else
    Q_UNUSED(before)
    Q_UNUSED(text)
#endif
}

void LinkAnnotationLayer::relayout()
{
    const qreal scale = m_canvas->screenScale();
    for (const Entry &entry : std::as_const(m_entries)) {
        if (!entry.widget || entry.page < 0
                || entry.page >= m_canvas->pageLabelCount())
            continue;
        const QLabel *label = m_canvas->pageLabel(entry.page);
        if (!label) continue;
        const QRectF rect(entry.bounds.topLeft() * scale + QPointF(label->pos()),
                          entry.bounds.size() * scale);
        entry.widget->setGeometry(rect.toAlignedRect());
        entry.widget->raise();
    }
}
