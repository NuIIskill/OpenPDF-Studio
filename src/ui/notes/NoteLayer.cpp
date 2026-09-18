#include "ui/notes/NoteLayer.hpp"

#include "ui/theme/Theme.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/PdfBackend.hpp"
#  include "engine/edit/EditSession.hpp"
#endif

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

class NoteUndoCommand : public QUndoCommand
{
public:
    NoteUndoCommand(NoteLayer *layer, QList<NoteData> before,
                    QList<NoteData> after, const QString &text)
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
    NoteLayer *m_layer { nullptr };
    QList<NoteData> m_before;
    QList<NoteData> m_after;
    bool m_firstRedo { true };
};

NoteLayer::NoteLayer(PageCanvas *canvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
{}

#ifdef HAVE_PDF_RENDERING
void NoteLayer::setSource(PdfBackend *backend, EditSession *session, QUndoStack *undo)
{
    m_backend = backend;
    m_session = session;
    m_undo    = undo;
}
#endif

void NoteLayer::reload()
{
    clear();
#ifdef HAVE_PDF_RENDERING
    if (!m_backend) return;
    for (int page = 0; page < m_canvas->pageCount(); ++page) {
        const QList<PdfBackend::Note> source = m_backend->pageNotes(page);
        for (const PdfBackend::Note &item : source) {
            NoteData note;
            note.id = item.id.isEmpty()
                ? QUuid::createUuid().toString(QUuid::WithoutBraces) : item.id;
            note.title          = item.title;
            note.text           = item.text;
            note.page           = page;
            note.pdfBounds      = item.bounds;
            note.modified       = QDateTime::currentDateTime();
            note.existing       = true;
            note.originalId     = item.id;
            note.originalTitle  = item.title;
            note.originalText   = item.text;
            note.originalBounds = item.bounds;
            note.pinned          = item.pinned;
            note.originalPinned  = item.pinned;
            addEntry(std::move(note), false);
        }
    }
    relayout();
#endif
    notifyChanged();
}

void NoteLayer::clear()
{
    hidePopup();
    for (const Entry &entry : std::as_const(m_entries)) delete entry.marker;
    m_entries.clear();
    m_activeId.clear();
    notifyChanged();
}

void NoteLayer::addEntry(NoteData note, bool position)
{
    auto *marker = new QPushButton(m_canvas->canvasWidget());
    marker->setObjectName(QStringLiteral("NoteMarker"));
    marker->setCursor(Qt::PointingHandCursor);
    marker->setIcon(Theme::makeIcon(QStringLiteral("message-square"), Qt::white,
                                    Qt::white, Qt::white, 15));
    marker->setIconSize(QSize(15, 15));
    marker->setFixedSize(26, 26);
    marker->setToolTip(note.title.isEmpty() ? tr("Note") : note.title);
    const QString id = note.id;
    connect(marker, &QPushButton::clicked, this, [this, id]() { activate(id); });
    m_entries.append({ std::move(note), marker });
    if (position) relayout();
}

void NoteLayer::setToolActive(bool active)
{
    m_toolActive = active;
    for (const Entry &entry : std::as_const(m_entries))
        if (entry.marker) entry.marker->setVisible(!entry.data.removed);
    if (!active) hidePopup();
}

void NoteLayer::addAt(const QPoint &canvasPos)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_session) return;
#endif
    auto [page, label] = m_canvas->pageAtCanvasPos(canvasPos);
    if (page < 0 || !label) return;

    const QList<NoteData> before = state();
    const qreal scale = m_canvas->screenScale();
    QPointF local = QPointF(canvasPos - label->pos()) / scale;
    const QSizeF pageSize = QSizeF(label->size()) / scale;
    local.setX(qBound(0.0, local.x(), qMax(0.0, pageSize.width() - 22.0)));
    local.setY(qBound(0.0, local.y(), qMax(0.0, pageSize.height() - 22.0)));
    NoteData note;
    note.id        = QUuid::createUuid().toString(QUuid::WithoutBraces);
    note.title     = tr("New note");
    note.page      = page;
    note.pdfBounds = QRectF(local, QSizeF(22.0, 22.0));
    note.modified  = QDateTime::currentDateTime();
    addEntry(note);
    syncSession();
    pushUndo(before, tr("Add note"));
    notifyChanged();
    Q_EMIT noteAdded(page);
    activate(note.id);
}

void NoteLayer::addAtPageCenter(int page)
{
    if (page < 0 || page >= m_canvas->pageLabelCount()) return;
    const QLabel *label = m_canvas->pageLabel(page);
    if (!label) return;
    addAt(label->geometry().center());
}

int NoteLayer::indexOf(const QString &id) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).data.id == id) return i;
    return -1;
}

void NoteLayer::activate(const QString &id)
{
    const int index = indexOf(id);
    if (index < 0 || m_entries.at(index).data.removed) return;
    m_activeId = id;
    showPopup(id);
    Q_EMIT noteActivated(id, m_entries.at(index).data.page);
}

void NoteLayer::update(const QString &id, const QString &title, const QString &text)
{
    const int index = indexOf(id);
    if (index < 0) return;
    const QList<NoteData> before = state();
    NoteData &note = m_entries[index].data;
    const QString effectiveTitle = title.isEmpty() ? tr("Untitled note") : title;
    if (note.title == effectiveTitle && note.text == text) return;
    note.title    = effectiveTitle;
    note.text     = text;
    note.modified = QDateTime::currentDateTime();
    m_entries[index].marker->setToolTip(note.title);
    syncSession();
    pushUndo(before, tr("Edit note"));
    notifyChanged();
    showPopup(id);
    Q_EMIT noteEdited(note.page);
}

void NoteLayer::remove(const QString &id)
{
    const int index = indexOf(id);
    if (index < 0) return;
    const QList<NoteData> before = state();
    const int page = m_entries.at(index).data.page;
    if (m_entries.at(index).data.existing) {
        m_entries[index].data.removed = true;
        m_entries[index].marker->hide();
    } else {
        delete m_entries.at(index).marker;
        m_entries.removeAt(index);
    }
    if (m_activeId == id) {
        m_activeId.clear();
        hidePopup();
    }
    syncSession();
    pushUndo(before, tr("Delete note"));
    notifyChanged();
    Q_EMIT noteRemoved(page);
}

void NoteLayer::setPinned(const QString &id, bool pinned)
{
    const int index = indexOf(id);
    if (index < 0 || m_entries.at(index).data.pinned == pinned) return;
    const QList<NoteData> before = state();
    m_entries[index].data.pinned = pinned;
    m_entries[index].data.modified = QDateTime::currentDateTime();
    syncSession();
    pushUndo(before, pinned ? tr("Pin note") : tr("Unpin note"));
    notifyChanged();
    Q_EMIT noteEdited(m_entries.at(index).data.page);
}

QList<NoteData> NoteLayer::notes() const
{
    QList<NoteData> result = state();
    std::stable_sort(result.begin(), result.end(), [](const NoteData &a, const NoteData &b) {
        if (a.removed != b.removed) return !a.removed;
        if (a.pinned != b.pinned) return a.pinned;
        return a.modified > b.modified;
    });
    return result;
}

QList<NoteData> NoteLayer::state() const
{
    QList<NoteData> result;
    result.reserve(m_entries.size());
    for (const Entry &entry : m_entries) result.append(entry.data);
    return result;
}

void NoteLayer::restoreState(const QList<NoteData> &restored)
{
    hidePopup();
    for (const Entry &entry : std::as_const(m_entries)) delete entry.marker;
    m_entries.clear();
    for (const NoteData &note : restored) {
        addEntry(note, false);
        m_entries.last().marker->setVisible(!note.removed);
    }
    relayout();
    syncSession();
    notifyChanged();
}

void NoteLayer::pushUndo(const QList<NoteData> &before, const QString &text)
{
#ifdef HAVE_PDF_RENDERING
    const QList<NoteData> after = state();
    if (!m_undo || before == after) return;
    m_undo->push(new NoteUndoCommand(this, before, after, text));
#else
    Q_UNUSED(before)
    Q_UNUSED(text)
#endif
}

void NoteLayer::syncSession()
{
#ifdef HAVE_PDF_RENDERING
    if (!m_session) return;
    QList<EditSession::NoteEdit> edits;
    for (const Entry &entry : std::as_const(m_entries)) {
        const NoteData &note = entry.data;
        if (note.existing && !note.removed
                && note.title == note.originalTitle
                && note.text == note.originalText
                && note.pdfBounds == note.originalBounds
                && note.pinned == note.originalPinned)
            continue;
        EditSession::NoteEdit edit;
        edit.page           = note.page;
        edit.id             = note.id;
        edit.originalId     = note.originalId;
        edit.title          = note.title;
        edit.text           = note.text;
        edit.originalText   = note.originalText;
        edit.pdfBounds      = note.pdfBounds;
        edit.originalBounds = note.originalBounds;
        edit.existing       = note.existing;
        edit.removed        = note.removed;
        edit.pinned         = note.pinned;
        edit.originalPinned = note.originalPinned;
        edits.append(std::move(edit));
    }
    m_session->replaceNoteEdits(std::move(edits));
#endif
}

void NoteLayer::notifyChanged()
{
    Q_EMIT notesChanged(notes());
}

void NoteLayer::relayout()
{
    const qreal scale = m_canvas->screenScale();
    for (const Entry &entry : std::as_const(m_entries)) {
        if (!entry.marker || entry.data.page < 0
                || entry.data.page >= m_canvas->pageLabelCount()) continue;
        const QLabel *label = m_canvas->pageLabel(entry.data.page);
        if (!label) continue;
        const QPointF topLeft = entry.data.pdfBounds.topLeft() * scale
                              + QPointF(label->pos());
        entry.marker->move(topLeft.toPoint());
        entry.marker->raise();
    }
    if (!m_activeId.isEmpty()) showPopup(m_activeId);
}

void NoteLayer::hidePopup()
{
    delete m_popup;
    m_popup = nullptr;
}

void NoteLayer::showPopup(const QString &id)
{
    hidePopup();
    const int index = indexOf(id);
    if (index < 0 || !m_entries.at(index).marker) return;
    const NoteData &note = m_entries.at(index).data;

    m_popup = new QFrame(m_canvas->canvasWidget());
    m_popup->setObjectName(QStringLiteral("NotePopup"));
    m_popup->setFixedWidth(260);
    auto *layout = new QVBoxLayout(m_popup);
    layout->setContentsMargins(14, 12, 12, 12);
    layout->setSpacing(8);
    auto *top = new QHBoxLayout;
    auto *dot = new QLabel(QStringLiteral("●"), m_popup);
    dot->setObjectName(QStringLiteral("NoteDot"));
    top->addWidget(dot);
    auto *title = new QLabel(note.title, m_popup);
    title->setObjectName(QStringLiteral("NotePopupTitle"));
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    top->addWidget(title, 1);
    auto *close = new QPushButton(m_popup);
    close->setObjectName(QStringLiteral("NotesClose"));
    close->setIcon(Theme::makeIcon(QStringLiteral("x"), Theme::IconMuted));
    close->setFixedSize(24, 24);
    connect(close, &QPushButton::clicked, this, &NoteLayer::hidePopup);
    top->addWidget(close);
    layout->addLayout(top);
    if (!note.text.isEmpty()) {
        auto *body = new QLabel(note.text, m_popup);
        body->setObjectName(QStringLiteral("NotePopupBody"));
        body->setTextFormat(Qt::PlainText);
        body->setWordWrap(true);
        layout->addWidget(body);
    }
    auto *page = new QLabel(tr("Page %1").arg(note.page + 1), m_popup);
    page->setObjectName(QStringLiteral("NoteCardMeta"));
    layout->addWidget(page);
    m_popup->adjustSize();

    const QRect canvasRect = m_canvas->canvasWidget()->rect();
    QPoint pos = m_entries.at(index).marker->geometry().topRight() + QPoint(8, -6);
    if (pos.x() + m_popup->width() > canvasRect.right())
        pos.setX(m_entries.at(index).marker->x() - m_popup->width() - 8);
    pos.setY(qBound(canvasRect.top(), pos.y(),
                    qMax(canvasRect.top(), canvasRect.bottom() - m_popup->height())));
    m_popup->move(pos);
    m_popup->show();
    m_popup->raise();
}
