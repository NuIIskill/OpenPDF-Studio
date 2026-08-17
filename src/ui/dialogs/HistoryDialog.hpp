#pragma once

#include "app/DocumentHistory.hpp"

#include <QDialog>
#include <QList>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;
QT_END_NAMESPACE

class HistoryRow;

// The change log of the open document, as a timeline the user can step back
// into. Reads a DocumentHistory and asks the DocumentView (through signals) to
// carry out what the buttons stand for — the dialog itself changes nothing.
class HistoryDialog : public QDialog
{
    Q_OBJECT

public:
    HistoryDialog(DocumentHistory *history, const QString &documentName,
                  QWidget *parent = nullptr);

    void retranslateUi();

    /// Enables/disables the two stack buttons. The document view owns that
    /// knowledge, so it pushes it in rather than the dialog guessing.
    void setUndoRedoAvailable(bool canUndo, bool canRedo);

    /// Headline and detail line for one entry — also used by the tooltips.
    static QString titleFor(const DocumentHistory::Entry &e);
    static QString detailFor(const DocumentHistory::Entry &e);

Q_SIGNALS:
    /// Go back to the state entry `index` describes.
    void restoreRequested(int index);
    void undoRequested();
    void redoRequested();
    /// Forget everything but the current state.
    void clearRequested();

protected:
    void changeEvent(QEvent *e) override;

private:
    void buildUi();
    void rebuildList();
    void updateButtons();
    void selectRow(int index);
    void applyStyle();
    // Confirms first when going back means dropping edits that are in no file.
    void requestRestore(int index);

    static QString iconFor(DocumentHistory::Kind kind);

    DocumentHistory *m_history { nullptr };
    QString          m_documentName;
    int              m_selected { -1 };
    bool             m_canUndo  { false };
    bool             m_canRedo  { false };

    QLabel      *m_title    { nullptr };
    QLabel      *m_titleIcon{ nullptr };
    QLabel      *m_subtitle { nullptr };
    QLabel      *m_empty    { nullptr };
    QScrollArea *m_scroll   { nullptr };
    QWidget     *m_list     { nullptr };
    QVBoxLayout *m_listLayout { nullptr };
    QPushButton *m_restoreBtn { nullptr };
    QPushButton *m_undoBtn    { nullptr };
    QPushButton *m_redoBtn    { nullptr };
    QPushButton *m_clearBtn   { nullptr };
    QPushButton *m_closeBtn   { nullptr };

    QList<HistoryRow *> m_rows;
};
