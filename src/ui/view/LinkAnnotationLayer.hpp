#pragma once

#include "ui/view/PageCanvas.hpp"
#include "ui/view/TextSelectionController.hpp"

#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QString>

QT_BEGIN_NAMESPACE
class QFrame;
class QUndoStack;
QT_END_NAMESPACE

class EditSession;
class LinkUndoCommand;
class PdfBackend;

/// Link hit areas and link-tool interaction for the document canvas.
class LinkAnnotationLayer : public QObject
{
    Q_OBJECT

public:
    explicit LinkAnnotationLayer(PageCanvas *canvas, QObject *parent = nullptr);

#ifdef HAVE_PDF_RENDERING
    void setSource(PdfBackend *backend, EditSession *session, QUndoStack *undo);
#endif

    void reload();
    void clear();
    void setToolActive(bool active);
    void relayout();

    void addInRect(const QRect &canvasRect);
    bool showEmptyContextMenu(
        const QPoint &canvasPos, const QPoint &globalPos,
        const QList<TextSelectionController::SelectionPart> &selection);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    void pageNeedsRerender(int page);
    void linkAdded(int page, int count);
    void linkEdited(int page);
    void linkRemoved(int page);

private:
    struct Entry {
        int     page { -1 };
        QRectF  originalBounds;
        QRectF  bounds;
        QString originalUrl;
        QString url;
        QList<QRectF> textRects;
        bool    existing { false };
        bool    removed  { false };
        bool    colorText { false };
        QFrame *widget   { nullptr };
    };

    struct State {
        int     page { -1 };
        QRectF  originalBounds;
        QRectF  bounds;
        QString originalUrl;
        QString url;
        QList<QRectF> textRects;
        bool    existing { false };
        bool    removed  { false };
        bool    colorText { false };

        bool operator==(const State &) const = default;
    };

    void addEntry(Entry entry, bool position = true);
    void addAt(const QPoint &canvasPos);
    bool addSelection(const QList<TextSelectionController::SelectionPart> &selection);
    void showContextMenu(QFrame *widget, const QPoint &globalPos);
    QString askForUrl(const QString &current = {});
    void syncSession();
    QList<State> state() const;
    void restoreState(const QList<State> &state);
    void pushUndo(const QList<State> &before, const QString &text);
    int indexOf(QFrame *widget) const;

    friend class LinkUndoCommand;

    PageCanvas  *m_canvas { nullptr };
    QList<Entry> m_entries;
    QFrame      *m_pressedLink { nullptr };
    QPoint       m_pressGlobal;

#ifdef HAVE_PDF_RENDERING
    PdfBackend  *m_backend { nullptr };
    EditSession *m_session { nullptr };
    QUndoStack  *m_undo { nullptr };
#endif
};
