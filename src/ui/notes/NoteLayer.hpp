#pragma once

#include "ui/notes/Note.hpp"
#include "ui/view/PageCanvas.hpp"

#include <QList>
#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QFrame;
class QPushButton;
class QUndoStack;
QT_END_NAMESPACE

class EditSession;
class PdfBackend;
class NoteUndoCommand;

/// Page markers and document state for PDF text notes.
class NoteLayer : public QObject
{
    Q_OBJECT

public:
    explicit NoteLayer(PageCanvas *canvas, QObject *parent = nullptr);

#ifdef HAVE_PDF_RENDERING
    void setSource(PdfBackend *backend, EditSession *session, QUndoStack *undo);
#endif

    void reload();
    void clear();
    void relayout();
    void setToolActive(bool active);
    void addAt(const QPoint &canvasPos);
    void addAtPageCenter(int page);
    void activate(const QString &id);
    void update(const QString &id, const QString &title, const QString &text);
    void remove(const QString &id);
    void setPinned(const QString &id, bool pinned);
    QList<NoteData> notes() const;

Q_SIGNALS:
    void notesChanged(const QList<NoteData> &notes);
    void noteActivated(const QString &id, int page);
    void noteAdded(int page);
    void noteEdited(int page);
    void noteRemoved(int page);

private:
    struct Entry {
        NoteData    data;
        QPushButton *marker { nullptr };
    };

    void addEntry(NoteData note, bool position = true);
    void showPopup(const QString &id);
    void hidePopup();
    void syncSession();
    void notifyChanged();
    QList<NoteData> state() const;
    void restoreState(const QList<NoteData> &state);
    void pushUndo(const QList<NoteData> &before, const QString &text);
    int indexOf(const QString &id) const;

    friend class NoteUndoCommand;

    PageCanvas *m_canvas { nullptr };
    QList<Entry> m_entries;
    QString      m_activeId;
    QFrame      *m_popup { nullptr };
    bool         m_toolActive { false };

#ifdef HAVE_PDF_RENDERING
    PdfBackend  *m_backend { nullptr };
    EditSession *m_session { nullptr };
    QUndoStack  *m_undo { nullptr };
#endif
};
