#pragma once

#include "ui/notes/Note.hpp"

#include <QList>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTextEdit;
class QVBoxLayout;
QT_END_NAMESPACE

/// Side panel for browsing and editing document notes.
class NotesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit NotesPanel(QWidget *parent = nullptr);

    void setNotes(const QList<NoteData> &notes);
    void setSelectedNote(const QString &id);
    void setDocumentAvailable(bool available);
    void retranslateUi();
    void refreshTheme();

Q_SIGNALS:
    void closeRequested();
    void newNoteRequested();
    void noteSelected(const QString &id);
    void saveRequested(const QString &id, const QString &title,
                       const QString &text);
    void deleteRequested(const QString &id);
    void pinRequested(const QString &id, bool pinned);

private:
    void rebuildList();
    void showSelection();
    const NoteData *selectedNote() const;

    QList<NoteData> m_notes;
    QString         m_selectedId;
    QLabel         *m_title { nullptr };
    QPushButton    *m_close { nullptr };
    QPushButton    *m_new { nullptr };
    QScrollArea    *m_scroll { nullptr };
    QWidget        *m_listWidget { nullptr };
    QVBoxLayout    *m_listLayout { nullptr };
    QLabel         *m_empty { nullptr };
    QWidget        *m_editor { nullptr };
    QLineEdit      *m_titleEdit { nullptr };
    QTextEdit      *m_textEdit { nullptr };
    QPushButton    *m_pin { nullptr };
    QPushButton    *m_delete { nullptr };
    QPushButton    *m_cancel { nullptr };
    QPushButton    *m_save { nullptr };
};
