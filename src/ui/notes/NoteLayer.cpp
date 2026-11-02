#include "ui/notes/NoteLayer.hpp"

#include "ui/notes/NoteColorRow.hpp"
#include "ui/notes/NoteDrag.hpp"
#include "ui/theme/Theme.hpp"
#include "ui/view/TextSelectionController.hpp"

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/PdfBackend.hpp"
#  include "engine/edit/EditSession.hpp"
#endif

#include <QDataStream>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace {

constexpr qreal kNoteSize = 22.0;

QRectF markerBeside(const QList<QRectF> &rects)
{
    const QRectF last = rects.last();
    return QRectF(last.right() + 2.0, last.center().y() - kNoteSize / 2, kNoteSize, kNoteSize);
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void paintMarker(QPushButton *marker, const QColor &color)
{
    marker->setStyleSheet(QStringLiteral("QPushButton#NoteMarker { background: %1; }"
                                         "QPushButton#NoteMarker:hover { background: %2; }")
                              .arg(color.name(), color.darker(112).name()));
}

}

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
    bool adds(const QString &id) const
    {
        const auto has = [&id](const QList<NoteData> &notes) {
            return std::any_of(notes.cbegin(), notes.cend(),
                               [&id](const NoteData &n) { return n.id == id; });
        };
        return has(m_after) && !has(m_before);
    }
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

#ifdef HAVE_PDF_RENDERING
void NoteLayer::addPage(int page, const QList<PdfBackend::Note> &notes)
{
    for (const PdfBackend::Note &item : notes) {
        NoteData note;
        note.id = item.id.isEmpty() ? newId() : item.id;
        note.title          = item.title;
        note.text           = item.text;
        note.page           = page;
        note.textRects      = item.textRects;
        note.quote          = item.quote;
        note.pdfBounds      = item.textRects.isEmpty() ? item.bounds : markerBeside(item.textRects);
        note.modified       = QDateTime::currentDateTime();
        note.existing       = true;
        note.originalId     = item.id;
        note.originalTitle  = item.title;
        note.originalText   = item.text;
        note.originalBounds = item.bounds;
        note.pinned          = item.pinned;
        note.originalPinned  = item.pinned;
        note.color           = item.color.isValid() ? item.color : NoteColorRow::standard();
        note.originalColor   = note.color;
        addEntry(std::move(note), false);
    }
    if (notes.isEmpty()) return;
    relayout();
    notifyChanged();
}
#endif

void NoteLayer::clear()
{
    hidePopup();
    clearPreview();
    for (const Entry &entry : std::as_const(m_entries)) delete entry.marker;
    m_entries.clear();
    m_activeId.clear();
    m_hiddenHighlights.clear();
    showHighlights();
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
    marker->setVisible(!note.removed);
    paintMarker(marker, note.color.isValid() ? note.color : NoteColorRow::standard());
    const QString id = note.id;
    connect(marker, &QPushButton::clicked, this, [this, id]() { activate(id); });
    marker->installEventFilter(this);
    auto *drag = new NoteDrag(marker, NoteDrag::Grab::PressPoint);
    drag->setEnabled(m_toolActive);
    // Hiding the marker would end the mouse grab the drag runs on, so it only fades.
    connect(drag, &NoteDrag::started, this, [this, marker]() {
        hidePopup();
        auto *fade = new QGraphicsOpacityEffect(marker);
        fade->setOpacity(0.3);
        marker->setGraphicsEffect(fade);
    });
    connect(drag, &NoteDrag::moved, this, &NoteLayer::previewAt);
    connect(drag, &NoteDrag::dropped, this, [this, id, marker](const QPoint &globalTopLeft) {
        marker->setGraphicsEffect(nullptr);
        dropAt(id, {}, {}, globalTopLeft);
    });
    m_entries.append({ std::move(note), marker, drag });
    if (position) relayout();
}

void NoteLayer::setToolActive(bool active)
{
    m_toolActive = active;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.marker) entry.marker->setVisible(!entry.data.removed);
        if (entry.drag) entry.drag->setEnabled(active);
    }
    if (!active) deactivate();
}

QPointF NoteLayer::onPage(int page, const QPointF &topLeft) const
{
    const QLabel *label = m_canvas->pageLabel(page);
    if (!label) return topLeft;
    const QSizeF pageSize = QSizeF(label->size()) / m_canvas->screenScale();
    return QPointF(qBound(0.0, topLeft.x(), qMax(0.0, pageSize.width() - kNoteSize)),
                   qBound(0.0, topLeft.y(), qMax(0.0, pageSize.height() - kNoteSize)));
}

NoteLayer::Target NoteLayer::targetAt(const QPoint &canvasPos, bool linkText) const
{
    Target target;
    auto [page, label] = m_canvas->pageAtCanvasPos(canvasPos);
    if (page < 0 || !label) return target;
    const qreal scale = m_canvas->screenScale();
    target.page    = page;
    target.topLeft = onPage(page, QPointF(canvasPos - label->pos()) / scale);
#ifdef HAVE_PDF_RENDERING
    // Let go over text, a note takes the sentence under the middle of the marker.
    if (linkText && m_backend) {
        const QPointF middle = QPointF(canvasPos - label->pos()) / scale
                             + QPointF(NoteDrag::MarkerSize, NoteDrag::MarkerSize) / (2 * scale);
        const PdfBackend::Selection sentence = m_backend->sentenceAt(page, middle);
        if (!sentence.rects.isEmpty()) {
            target.textRects = sentence.rects;
            target.quote     = sentence.text;
            target.topLeft   = onPage(page, markerBeside(sentence.rects).topLeft());
        }
    }
#else
    Q_UNUSED(linkText)
#endif
    return target;
}

bool NoteLayer::addAt(const QPoint &canvasPos)
{
    return addNote(targetAt(canvasPos, false), {}, {}, {});
}

bool NoteLayer::addNote(const Target &target, const QString &title, const QString &text,
                        const QColor &color)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_session) return false;
#endif
    if (target.page < 0) return false;
    if (m_beforeChange) m_beforeChange();

    const QList<NoteData> before = state();
    NoteData note;
    note.id        = newId();
    note.title     = title.isEmpty() ? tr("New note") : title;
    note.text      = text;
    note.page      = target.page;
    note.pdfBounds = QRectF(target.topLeft, QSizeF(kNoteSize, kNoteSize));
    note.textRects = target.textRects;
    note.quote     = target.quote;
    note.color     = color.isValid() ? color : NoteColorRow::standard();
    note.modified  = QDateTime::currentDateTime();
    addEntry(note);
    syncSession();
    pushUndo(before, tr("Add note"));
    notifyChanged();
    Q_EMIT noteAdded(target.page);
    activate(note.id);
    return true;
}

void NoteLayer::press(const QPoint &canvasPos)
{
    m_pressAt = canvasPos;
    if (m_selection) m_selection->handlePress(canvasPos);
}

void NoteLayer::release()
{
    if (m_pressAt.x() < 0) return;
    const QPoint pressAt = std::exchange(m_pressAt, QPoint(-1, -1));
    // A click places a note; a drag over text marks it and the note keeps it.
    const bool marked = m_selection && m_selection->handleRelease()
                     && m_selection->hasSelection();
    if (!marked) {
        if (m_selection) m_selection->clear();
        // With a note open, a click on the page only closes it, so clicking
        // away never leaves a trail of new notes.
        if (m_popup) {
            deactivate();
            return;
        }
        if (addAt(pressAt)) m_clickedId = m_activeId;
        return;
    }
    const QList<TextSelectionController::SelectionPart> parts = m_selection->selectedParts();
    const QString quote = m_selection->selectedText().simplified();
    m_selection->clear();
    for (const auto &part : parts) {
        if (part.rects.isEmpty()) continue;
        Target target;
        target.page      = part.page;
        target.textRects = part.rects;
        target.quote     = quote;
        target.topLeft   = onPage(part.page, markerBeside(part.rects).topLeft());
        // With a note open, what is marked becomes that note's text.
        if (m_popup && !m_activeId.isEmpty()) {
            const QString id = m_activeId;
            placeAt(id, target, tr("Link note"));
        } else {
            addNote(target, {}, {}, {});
        }
        return;
    }
}

bool NoteLayer::dropAt(const QString &id, const QString &title, const QString &text,
                       const QPoint &globalTopLeft, const QColor &color)
{
    clearPreview();
    m_previewWatch = false;
    const Target target = targetAt(m_canvas->canvasWidget()->mapFromGlobal(globalTopLeft), true);
    if (id.isEmpty()) return addNote(target, title, text, color);
    return placeAt(id, target, target.textRects.isEmpty() ? tr("Move note") : tr("Link note"));
}

void NoteLayer::previewAt(const QPoint &globalTopLeft)
{
    watchCanvas();
    m_previewGlobal = globalTopLeft;
    m_previewWatch  = true;
    const Target target = targetAt(m_canvas->canvasWidget()->mapFromGlobal(globalTopLeft), true);
    if (target.textRects.isEmpty()) {
        clearPreview();
        return;
    }
    if (target.page == m_previewTarget.page && target.textRects == m_previewTarget.textRects)
        return;
    clearPreview();
    m_previewTarget = target;
    m_preview = paint(target.page, target.textRects, QStringLiteral("NoteLinkPreview"));
}

void NoteLayer::clearPreview()
{
    qDeleteAll(m_preview);
    m_preview.clear();
    m_previewTarget = {};
}

bool NoteLayer::placeAt(const QString &id, const Target &target, const QString &undoText)
{
    const int index = indexOf(id);
    if (index < 0 || m_entries.at(index).data.removed) return false;
#ifdef HAVE_PDF_RENDERING
    if (!m_session) return false;
#endif
    if (target.page < 0) return false;

    if (m_beforeChange) m_beforeChange();
    const QList<NoteData> before = state();
    NoteData moved = m_entries.at(index).data;
    const bool relinked = moved.textRects != target.textRects;
    moved.pdfBounds.moveTopLeft(target.topLeft);
    moved.textRects = target.textRects;
    moved.quote     = target.quote;
    moved.modified  = QDateTime::currentDateTime();
    if (moved.existing && (moved.page != target.page || relinked)) {
        // The writer looks for a note on its own page and of its own kind,
        // so it is removed there and written anew.
        m_entries[index].data.removed = true;
        m_entries[index].marker->hide();
        moved.id = newId();
        moved.existing = false;
        moved.originalId.clear();
        moved.page = target.page;
        addEntry(moved, false);
    } else {
        moved.page = target.page;
        m_entries[index].data = moved;
    }
    relayout();
    syncSession();
    pushUndo(before, undoText);
    notifyChanged();
    Q_EMIT noteEdited(target.page);
    activate(moved.id);
    return true;
}

void NoteLayer::unlink(const QString &id)
{
    const int index = indexOf(id);
    if (index < 0 || m_entries.at(index).data.textRects.isEmpty()) return;
    Target target;
    target.page    = m_entries.at(index).data.page;
    target.topLeft = m_entries.at(index).data.pdfBounds.topLeft();
    placeAt(id, target, tr("Unlink note"));
}

QWidget *NoteLayer::marker(const QString &id) const
{
    const int index = indexOf(id);
    return index < 0 ? nullptr : m_entries.at(index).marker;
}

int NoteLayer::indexOf(const QString &id) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).data.id == id) return i;
    return -1;
}

void NoteLayer::activate(const QString &id)
{
    if (!m_activeId.isEmpty() && m_activeId != id && pristine(m_activeId)) deactivate();
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
    if (m_beforeChange) m_beforeChange();
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
    if (m_beforeChange) m_beforeChange();
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
    showHighlights();
    syncSession();
    pushUndo(before, tr("Delete note"));
    notifyChanged();
    Q_EMIT noteRemoved(page);
}

void NoteLayer::setPinned(const QString &id, bool pinned)
{
    const int index = indexOf(id);
    if (index < 0 || m_entries.at(index).data.pinned == pinned) return;
    if (m_beforeChange) m_beforeChange();
    const QList<NoteData> before = state();
    m_entries[index].data.pinned = pinned;
    m_entries[index].data.modified = QDateTime::currentDateTime();
    syncSession();
    pushUndo(before, pinned ? tr("Pin note") : tr("Unpin note"));
    notifyChanged();
    Q_EMIT noteEdited(m_entries.at(index).data.page);
}

void NoteLayer::setColor(const QString &id, const QColor &color)
{
    const int index = indexOf(id);
    if (index < 0 || !color.isValid() || m_entries.at(index).data.color == color) return;
    if (m_beforeChange) m_beforeChange();
    const QList<NoteData> before = state();
    NoteData changed = m_entries.at(index).data;
    changed.color    = color;
    changed.modified = QDateTime::currentDateTime();
    if (changed.existing && !changed.textRects.isEmpty()) {
        // The page draws a highlight from the file, so it is removed there and written anew.
        m_entries[index].data.removed = true;
        m_entries[index].marker->hide();
        changed.id = newId();
        changed.existing = false;
        changed.originalId.clear();
        addEntry(changed, false);
    } else {
        m_entries[index].data = changed;
        paintMarker(m_entries[index].marker, color);
    }
    relayout();
    syncSession();
    pushUndo(before, tr("Change note color"));
    notifyChanged();
    Q_EMIT noteEdited(changed.page);
    if (m_activeId == id && changed.id != id) activate(changed.id);
    else if (m_activeId == changed.id) showPopup(changed.id);
}

bool NoteLayer::colorAt(const QPoint &globalPos, const QColor &color)
{
    // A color let go over a marker or a linked text colors that note.
    const QPoint at = m_canvas->canvasWidget()->mapFromGlobal(globalPos);
    const auto [page, label] = m_canvas->pageAtCanvasPos(at);
    const QPointF onPage = label ? QPointF(at - label->pos()) / m_canvas->screenScale() : QPointF();
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.data.removed) continue;
        bool hit = entry.marker && entry.marker->isVisible() && entry.marker->geometry().contains(at);
        for (const QRectF &rect : entry.data.textRects)
            hit |= entry.data.page == page && rect.contains(onPage);
        if (!hit) continue;
        setColor(entry.data.id, color);
        return true;
    }
    return false;
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
    for (const NoteData &note : restored) addEntry(note, false);
    relayout();
    syncSession();
    notifyChanged();
}

QByteArray NoteLayer::encodedState() const
{
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    const QList<NoteData> notes = state();
    out << quint8(3) << qint32(notes.size());
    for (const NoteData &n : notes)
        out << n.id << n.title << n.text << qint32(n.page) << n.pdfBounds << n.modified
            << n.pinned << n.existing << n.removed << n.originalId << n.originalTitle
            << n.originalText << n.originalBounds << n.originalPinned
            << n.textRects << n.quote << n.color << n.originalColor;
    return data;
}

void NoteLayer::restoreEncoded(const QByteArray &data)
{
    QDataStream in(data);
    in.setVersion(QDataStream::Qt_6_0);
    quint8 version = 0;
    qint32 count = 0;
    in >> version >> count;
    if (version < 1 || version > 3 || count < 0) return;
    QList<NoteData> notes;
    for (qint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        NoteData n;
        qint32 page = -1;
        in >> n.id >> n.title >> n.text >> page >> n.pdfBounds >> n.modified
           >> n.pinned >> n.existing >> n.removed >> n.originalId >> n.originalTitle
           >> n.originalText >> n.originalBounds >> n.originalPinned;
        if (version >= 2) in >> n.textRects >> n.quote;
        if (version >= 3) in >> n.color >> n.originalColor;
        if (!n.color.isValid()) n.color = n.originalColor = NoteColorRow::standard();
        n.page = page;
        notes.append(std::move(n));
    }
    if (in.status() != QDataStream::Ok) return;
    if (m_beforeChange) m_beforeChange();
    if (notes != state()) restoreState(notes);
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
                && (!note.textRects.isEmpty() || note.pdfBounds == note.originalBounds)
                && note.pinned == note.originalPinned
                && note.color == note.originalColor)
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
        edit.textRects      = note.textRects;
        edit.color          = note.color;
        edit.originalColor  = note.originalColor;
        edits.append(std::move(edit));
    }
    m_session->replaceNoteEdits(std::move(edits));

    // A highlight in the file is drawn by the page, so its page shows again without it.
    QHash<QString, int> hidden;
    for (const Entry &entry : std::as_const(m_entries))
        if (entry.data.existing && entry.data.removed && !entry.data.textRects.isEmpty())
            hidden.insert(entry.data.id, entry.data.page);
    QSet<int> changed;
    for (auto it = hidden.cbegin(); it != hidden.cend(); ++it)
        if (!m_hiddenHighlights.contains(it.key())) changed.insert(it.value());
    for (auto it = m_hiddenHighlights.cbegin(); it != m_hiddenHighlights.cend(); ++it)
        if (!hidden.contains(it.key())) changed.insert(it.value());
    m_hiddenHighlights = hidden;
    for (int page : std::as_const(changed)) Q_EMIT pageNeedsRerender(page);
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
    }
    showHighlights();
    if (!m_activeId.isEmpty()) showPopup(m_activeId);
}

QList<QWidget *> NoteLayer::paint(int page, const QList<QRectF> &rects, const QString &name,
                                  const QColor &color) const
{
    QList<QWidget *> marks;
    const QLabel *label = page >= 0 && page < m_canvas->pageLabelCount()
        ? m_canvas->pageLabel(page) : nullptr;
    if (!label) return marks;
    const qreal scale = m_canvas->screenScale();
    for (const QRectF &rect : rects) {
        auto *mark = new QWidget(m_canvas->canvasWidget());
        mark->setObjectName(name);
        mark->setAttribute(Qt::WA_StyledBackground, true);
        mark->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        const QRectF shown(rect.topLeft() * scale + QPointF(label->pos()), rect.size() * scale);
        mark->setGeometry(shown.toAlignedRect());
        if (color.isValid())
            mark->setStyleSheet(QStringLiteral("background: rgba(%1, %2, %3, 0.38);")
                                    .arg(color.red()).arg(color.green()).arg(color.blue()));
        mark->show();
        marks.append(mark);
    }
    for (const Entry &entry : m_entries) if (entry.marker) entry.marker->raise();
    if (m_popup) m_popup->raise();
    return marks;
}

void NoteLayer::showHighlights()
{
    // What the file already has is drawn by the page itself.
    qDeleteAll(m_highlights);
    m_highlights.clear();
    for (const Entry &entry : std::as_const(m_entries))
        if (!entry.data.existing && !entry.data.removed && !entry.data.textRects.isEmpty())
            m_highlights += paint(entry.data.page, entry.data.textRects,
                                  QStringLiteral("NoteHighlight"), entry.data.color);
    for (const Entry &entry : std::as_const(m_entries)) if (entry.marker) entry.marker->raise();
}

bool NoteLayer::pristine(const QString &id) const
{
    // A note a click made and nothing touched since; its adding is still the last step.
#ifdef HAVE_PDF_RENDERING
    const int index = indexOf(id);
    if (id.isEmpty() || id != m_clickedId || index < 0 || !m_undo || m_undo->index() == 0)
        return false;
    const NoteData &note = m_entries.at(index).data;
    if (note.existing || note.removed || !note.text.isEmpty() || !note.textRects.isEmpty()
            || note.title != tr("New note"))
        return false;
    const auto *last = dynamic_cast<const NoteUndoCommand *>(m_undo->command(m_undo->index() - 1));
    return last && last->adds(id);
#else
    Q_UNUSED(id)
    return false;
#endif
}

void NoteLayer::deactivate()
{
    const QString id = std::exchange(m_activeId, QString());
    hidePopup();
    // Taken back through the document's own undo, so the history follows.
    if (pristine(id)) Q_EMIT undoRequested();
    m_clickedId.clear();
}
