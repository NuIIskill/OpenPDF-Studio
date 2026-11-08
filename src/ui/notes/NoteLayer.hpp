#pragma once

#include "engine/edit/TextLayout.hpp"
#include "ui/notes/Note.hpp"
#include "ui/view/PageCanvas.hpp"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/PdfBackend.hpp"
#endif

QT_BEGIN_NAMESPACE
class QFrame;
class QPushButton;
class QUndoCommand;
class QUndoStack;
class QWidget;
QT_END_NAMESPACE

class EditSession;
class NoteDrag;
class PdfBackend;
class NoteUndoCommand;
class TextSelectionController;

/// Page markers, text highlights and document state for PDF notes.
class NoteLayer : public QObject
{
    Q_OBJECT

public:
    explicit NoteLayer(PageCanvas *canvas, QObject *parent = nullptr);

#ifdef HAVE_PDF_RENDERING
    void setSource(PdfBackend *backend, EditSession *session, QUndoStack *undo);
#endif

#ifdef HAVE_PDF_RENDERING
    void addPage(int page, const QList<PdfBackend::Note> &notes);
#endif
    void setBeforeChange(std::function<void()> hook) { m_beforeChange = std::move(hook); }
    void setSelection(TextSelectionController *selection) { m_selection = selection; }

    QByteArray encodedState() const;
    void       restoreEncoded(const QByteArray &data);
    void clear();
    void relayout();
    void setToolActive(bool active);
    void press(const QPoint &canvasPos);
    void release();
    bool addAt(const QPoint &canvasPos);
    bool dropAt(const QString &id, const QString &title, const QString &text,
                const QPoint &globalTopLeft, const QColor &color = {});
    void previewAt(const QPoint &globalTopLeft);
    void unlink(const QString &id);
    void showFollowing(int fromPage, const QRectF &from, int toPage, const QPointF &by);
    void follow(int fromPage, const QRectF &from, int toPage, const QPointF &by,
                const QList<TextLayout::OriginalLine> &lines, QUndoCommand *step);
    QWidget *marker(const QString &id) const;
    void activate(const QString &id);
    void update(const QString &id, const QString &title, const QString &text);
    void remove(const QString &id);
    void setPinned(const QString &id, bool pinned);
    void setColor(const QString &id, const QColor &color);
    bool colorAt(const QPoint &globalPos, const QColor &color);
    QList<NoteData> notes() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    void notesChanged(const QList<NoteData> &notes);
    void noteActivated(const QString &id, int page);
    void noteAdded(int page);
    void noteEdited(int page);
    void noteRemoved(int page);
    void pageNeedsRerender(int page);
    void undoRequested();

private:
    struct Entry {
        NoteData    data;
        QPushButton *marker { nullptr };
        NoteDrag    *drag { nullptr };
    };

    struct Target {
        int           page { -1 };
        QPointF       topLeft;
        QList<QRectF> textRects;
        QString       quote;

        bool operator==(const Target &) const = default;
    };

    struct Following {
        int     fromPage { -1 };
        QRectF  from;
        int     toPage { -1 };
        QPointF by;
    };

    void addEntry(NoteData note, bool position = true);
    static bool follows(const NoteData &note, const Following &move);
    static NoteData carried(NoteData note, const Following &move);
    NoteData shown(const NoteData &note) const;
    Target targetAt(const QPoint &canvasPos, bool linkText) const;
    QPointF onPage(int page, const QPointF &topLeft) const;
    bool addNote(const Target &target, const QString &title, const QString &text,
                 const QColor &color);
    bool placeAt(const QString &id, const Target &target, const QString &undoText);
    QList<QWidget *> paint(int page, const QList<QRectF> &rects, const QString &name,
                           const QColor &color = {}) const;
    void showHighlights();
    void clearPreview();
    void deactivate();
    void carry();
    void watchCanvas();
    bool pristine(const QString &id) const;
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
    QString      m_popupId;
    QString      m_clickedId;
    QPoint       m_popupGrab { -1, -1 };
    QPoint       m_popupOffset;
    QPoint       m_markerOffset;
    QPoint       m_lastGlobal;
    QPoint       m_previewGlobal;
    bool         m_previewWatch { false };
    bool         m_popupDragging { false };
    QList<QWidget *> m_highlights;
    QList<QWidget *> m_preview;
    Target       m_previewTarget;
    Following    m_following;
    QHash<QString, int> m_hiddenHighlights;
    TextSelectionController *m_selection { nullptr };
    QPoint       m_pressAt { -1, -1 };
    bool         m_toolActive { false };
    std::function<void()> m_beforeChange;

#ifdef HAVE_PDF_RENDERING
    PdfBackend  *m_backend { nullptr };
    EditSession *m_session { nullptr };
    QUndoStack  *m_undo { nullptr };
#endif
};
