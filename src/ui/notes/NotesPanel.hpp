#pragma once

#include "ui/notes/Note.hpp"

#include <QList>
#include <QPointer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTextEdit;
class QVBoxLayout;
QT_END_NAMESPACE

class NoteColorRow;
class NoteDrag;

/// Side panel for browsing and editing document notes.
class NotesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit NotesPanel(QWidget *parent = nullptr);

    void setNotes(const QList<NoteData> &notes);
    void setSelectedNote(const QString &id);
    void setDocumentAvailable(bool available);
    void draftPlaced(bool placed);
    void setPalette(const QList<QColor> &colors);
    void setHints(bool colorHint, bool draftNudge, bool repeat);
    void retranslateUi();
    void refreshTheme();

Q_SIGNALS:
    void closeRequested();
    void placeRequested(const QString &id, const QPoint &globalTopLeft);
    void draftDropped(const QString &title, const QString &text, const QColor &color,
                      const QPoint &globalTopLeft);
    void dragHovered(const QPoint &globalTopLeft);
    void noteSelected(const QString &id);
    void saveRequested(const QString &id, const QString &title,
                       const QString &text);
    void deleteRequested(const QString &id);
    void pinRequested(const QString &id, bool pinned);
    void unlinkRequested(const QString &id);
    void colorRequested(const QString &id, const QColor &color);
    void colorDroppedOnPage(const QColor &color, const QPoint &globalPos);
    void paletteChanged(const QList<QColor> &colors);
    void colorHintSeen();
    void draftNudgeSeen();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void rebuildList();
    void select(const QString &id);
    void commitEdit();
    void revertEdit();
    void openDraft();
    void closeDraft();
    QWidget *makeCard(const NoteData &note);
    QWidget *makeDraft();
    NoteColorRow *makeColorRow(QWidget *parent);
    void colorDropped(const QColor &color, const QPoint &globalPos, int fromSlot);
    void keepColor(const QColor &color);
    void colorLearned();
    void markHintsShown();
    void tookColor(const QColor &color);
    void watchDrag(NoteDrag *drag);
    const NoteData *selectedNote() const;

    QList<NoteData>     m_notes;
    QString             m_selectedId;
    QLabel             *m_title { nullptr };
    QPushButton        *m_close { nullptr };
    QPushButton        *m_new { nullptr };
    QScrollArea        *m_scroll { nullptr };
    QWidget            *m_listWidget { nullptr };
    QVBoxLayout        *m_listLayout { nullptr };
    QPointer<QWidget>   m_card;
    QPointer<QLineEdit> m_titleEdit;
    QPointer<QTextEdit> m_textEdit;
    bool                m_draft { false };
    bool                m_droppingDraft { false };
    QString             m_draftTitle;
    QString             m_draftText;
    QColor              m_draftColor;
    QList<QColor>       m_palette;
    bool                m_colorHint { true };
    int                 m_hintStage { 1 };
    bool                m_draftNudge { true };
    bool                m_repeatHints { false };
    bool                m_colorHintShown { false };
    bool                m_draftNudgeShown { false };
    QColor              m_popColor;
    QColor              m_mixedColor;
    QPointer<QWidget>   m_draftSlot;
    QPointer<QTextEdit> m_draftTextEdit;
};
