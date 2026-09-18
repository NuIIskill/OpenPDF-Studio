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

/// The change log of the open document, as a timeline the user can step back into.
class HistoryDialog : public QDialog
{
    Q_OBJECT

public:
    HistoryDialog(DocumentHistory *history, const QString &documentName,
                  QWidget *parent = nullptr);

    void retranslateUi();

    void setUndoRedoAvailable(bool canUndo, bool canRedo);

    static QString titleFor(const DocumentHistory::Entry &e);
    static QString detailFor(const DocumentHistory::Entry &e);

Q_SIGNALS:

    void restoreRequested(int index);
    void undoRequested();
    void redoRequested();

    void clearRequested();

protected:
    void changeEvent(QEvent *e) override;

private:
    void buildUi();
    void rebuildList();
    void updateButtons();
    void selectRow(int index);
    void applyStyle();

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
