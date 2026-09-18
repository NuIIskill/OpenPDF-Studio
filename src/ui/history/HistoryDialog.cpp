#include "ui/history/HistoryDialog.hpp"
#include "ui/history/HistoryRow.hpp"
#include "ui/theme/Theme.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

HistoryDialog::HistoryDialog(DocumentHistory *history, const QString &documentName,
                             QWidget *parent)
    : QDialog(parent)
    , m_history(history)
    , m_documentName(documentName)
{
    setWindowTitle(tr("Change history — OpenPDF Studio"));
    setMinimumSize(620, 520);
    resize(700, 620);

    buildUi();
    applyStyle();

    if (m_history) {
        connect(m_history, &DocumentHistory::changed, this, [this]() {
            rebuildList();
        });
    }
    rebuildList();
}

void HistoryDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 18);
    root->setSpacing(0);

    auto *headerRow = new QHBoxLayout;
    headerRow->setSpacing(10);

    m_titleIcon = new QLabel(this);
    m_titleIcon->setFixedSize(24, 24);
    headerRow->addWidget(m_titleIcon);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("HistoryHeadline"));
    headerRow->addWidget(m_title);
    headerRow->addStretch(1);
    root->addLayout(headerRow);

    m_subtitle = new QLabel(this);
    m_subtitle->setObjectName(QStringLiteral("HistorySubtitle"));
    m_subtitle->setWordWrap(true);
    m_subtitle->setContentsMargins(0, 6, 0, 14);
    root->addWidget(m_subtitle);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("HistoryScroll"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_list = new QWidget;
    m_list->setObjectName(QStringLiteral("HistoryList"));
    m_listLayout = new QVBoxLayout(m_list);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(0);
    m_listLayout->addStretch(1);
    m_scroll->setWidget(m_list);
    root->addWidget(m_scroll, 1);

    m_empty = new QLabel(this);
    m_empty->setObjectName(QStringLiteral("HistorySubtitle"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->hide();
    root->addWidget(m_empty);

    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(0, 16, 0, 0);
    actions->setSpacing(10);

    m_restoreBtn = new QPushButton(this);
    m_restoreBtn->setObjectName(QStringLiteral("HistoryBtn"));
    m_restoreBtn->setCursor(Qt::PointingHandCursor);
    connect(m_restoreBtn, &QPushButton::clicked, this, [this]() {
        requestRestore(m_selected);
    });
    actions->addWidget(m_restoreBtn, 1);

    m_undoBtn = new QPushButton(this);
    m_undoBtn->setObjectName(QStringLiteral("HistoryBtn"));
    m_undoBtn->setCursor(Qt::PointingHandCursor);
    connect(m_undoBtn, &QPushButton::clicked, this, &HistoryDialog::undoRequested);
    actions->addWidget(m_undoBtn, 1);

    m_redoBtn = new QPushButton(this);
    m_redoBtn->setObjectName(QStringLiteral("HistoryBtn"));
    m_redoBtn->setCursor(Qt::PointingHandCursor);
    connect(m_redoBtn, &QPushButton::clicked, this, &HistoryDialog::redoRequested);
    actions->addWidget(m_redoBtn, 1);

    root->addLayout(actions);

    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(0, 14, 0, 0);
    footer->setSpacing(10);

    m_clearBtn = new QPushButton(this);
    m_clearBtn->setObjectName(QStringLiteral("HistoryClearBtn"));
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        if (!m_history || m_history->count() < 2) return;
        const auto answer = QMessageBox::question(
            this, tr("Clear history"),
            tr("Forget every recorded step except the one the document is at?\n\n"
               "The document itself is not changed — only the list of states you "
               "can go back to."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes) Q_EMIT clearRequested();
    });
    footer->addWidget(m_clearBtn);
    footer->addStretch(1);

    m_closeBtn = new QPushButton(this);
    m_closeBtn->setObjectName(QStringLiteral("HistoryCloseBtn"));
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setDefault(true);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(m_closeBtn);

    root->addLayout(footer);

    retranslateUi();
}

void HistoryDialog::retranslateUi()
{
    setWindowTitle(tr("Change history — OpenPDF Studio"));
    m_title->setText(tr("Change history"));
    m_subtitle->setText(m_documentName.isEmpty()
        ? tr("Every change made to this document, newest first.")
        : tr("Every change made to \"%1\", newest first.").arg(m_documentName));
    m_empty->setText(tr("No changes recorded yet."));
    m_restoreBtn->setText(tr("Go back to this state"));
    m_undoBtn->setText(tr("Undo"));
    m_redoBtn->setText(tr("Redo"));
    m_clearBtn->setText(tr("Clear history"));
    m_closeBtn->setText(tr("Close"));

    const QPixmap head = Theme::renderSvg(QStringLiteral("history"),
                                          Theme::IconNormal, 22);
    if (!head.isNull()) m_titleIcon->setPixmap(head);
    const auto setIcon = [](QPushButton *b, const QString &name) {
        const QPixmap px = Theme::renderSvg(name, Theme::IconNormal, 16);
        if (!px.isNull()) b->setIcon(QIcon(px));
    };
    setIcon(m_restoreBtn, QStringLiteral("refresh-cw"));
    setIcon(m_undoBtn,    QStringLiteral("undo-2"));
    setIcon(m_redoBtn,    QStringLiteral("redo-2"));
    const QPixmap trash = Theme::renderSvg(QStringLiteral("trash-2"),
                                           QColor(Theme::DarkMode ? QStringLiteral("#F87171")
                                                                  : QStringLiteral("#DC2626")),
                                           16);
    if (!trash.isNull()) m_clearBtn->setIcon(QIcon(trash));

    rebuildList();
}

void HistoryDialog::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) retranslateUi();
    QDialog::changeEvent(e);
}

void HistoryDialog::rebuildList()
{
    if (!m_listLayout) return;

    for (HistoryRow *row : std::as_const(m_rows)) {
        m_listLayout->removeWidget(row);
        row->hide();
        row->deleteLater();
    }
    m_rows.clear();

    const QList<DocumentHistory::Entry> entries =
        m_history ? m_history->entries() : QList<DocumentHistory::Entry>{};

    m_scroll->setVisible(!entries.isEmpty());
    m_empty->setVisible(entries.isEmpty());

    for (int i = static_cast<int>(entries.size()) - 1; i >= 0; --i) {
        const DocumentHistory::Entry &e = entries[i];
        auto *row = new HistoryRow(i, i + 1,
                                   e.time.toString(QStringLiteral("HH:mm")),
                                   titleFor(e), detailFor(e), iconFor(e.kind),
                                    i == entries.size() - 1,
                                     i == 0, m_list);
        connect(row, &HistoryRow::clicked, this, &HistoryDialog::selectRow);
        connect(row, &HistoryRow::doubleClicked, this, &HistoryDialog::requestRestore);
        connect(row, &HistoryRow::menuRequested, this,
                [this](int index, const QPoint &globalPos) {
            QMenu menu(this);
            QAction *back = menu.addAction(tr("Go back to this state"));
            QAction *copy = menu.addAction(tr("Copy details"));
            back->setEnabled(m_history && index != m_history->currentIndex()
                             && m_history->canRestore(index));
            QAction *chosen = menu.exec(globalPos);
            if (chosen == back) {
                requestRestore(index);
            } else if (chosen == copy && m_history
                       && index < m_history->entries().size()) {
                const DocumentHistory::Entry &entry = m_history->entries()[index];
                QString line = entry.time.toString(QStringLiteral("HH:mm"))
                             + QStringLiteral(" · ") + titleFor(entry);
                const QString detail = detailFor(entry);
                if (!detail.isEmpty()) line += QStringLiteral(" · ") + detail;
                QApplication::clipboard()->setText(line);
            }
        });
        m_listLayout->insertWidget(m_listLayout->count() - 1, row);
        m_rows.append(row);
    }

    const int current = m_history ? m_history->currentIndex() : -1;
    if (m_selected < 0 || m_selected >= entries.size()) m_selected = current;
    for (HistoryRow *row : std::as_const(m_rows))
        row->setSelected(row->index() == m_selected);

    updateButtons();
}

void HistoryDialog::selectRow(int index)
{
    m_selected = index;
    for (HistoryRow *row : std::as_const(m_rows))
        row->setSelected(row->index() == index);
    updateButtons();
}

void HistoryDialog::updateButtons()
{
    const int current = m_history ? m_history->currentIndex() : -1;
    m_restoreBtn->setEnabled(m_selected >= 0 && m_selected != current
                             && m_history->canRestore(m_selected));
    m_undoBtn->setEnabled(m_canUndo);
    m_redoBtn->setEnabled(m_canRedo);
    m_clearBtn->setEnabled(m_history && m_history->count() > 1);
}

void HistoryDialog::setUndoRedoAvailable(bool canUndo, bool canRedo)
{
    m_canUndo = canUndo;
    m_canRedo = canRedo;
    updateButtons();
}

void HistoryDialog::requestRestore(int index)
{
    if (!m_history || index < 0 || index >= m_history->count()) return;
    if (index == m_history->currentIndex()) return;

    if (m_history->restoringDropsEdits(index)) {
        const auto answer = QMessageBox::question(
            this, tr("Go back to this state"),
            tr("This state is part of an earlier version of the document, so it "
               "has to be loaded again.\n\n"
               "Text, image and drawing edits made since then are not part of any file "
               "yet and will be lost. Continue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }
    Q_EMIT restoreRequested(index);
}

QString HistoryDialog::titleFor(const DocumentHistory::Entry &e)
{
    using Kind = DocumentHistory::Kind;
    switch (e.kind) {
    case Kind::Opened:         return tr("Document opened");
    case Kind::TextEdited:     return tr("Text changed");
    case Kind::TextRemoved:    return tr("Text removed");
    case Kind::ImageInserted:  return tr("Image inserted");
    case Kind::ImageRemoved:   return tr("Image removed");
    case Kind::LinkAdded:      return tr("Link added");
    case Kind::LinkEdited:     return tr("Link changed");
    case Kind::LinkRemoved:    return tr("Link removed");
    case Kind::NoteAdded:      return tr("Note added");
    case Kind::NoteEdited:     return tr("Note changed");
    case Kind::NoteRemoved:    return tr("Note removed");
    case Kind::DrawingAdded:   return tr("Drawing added");
    case Kind::DrawingRemoved: return tr("Drawing removed");
    case Kind::PageRotated:    return e.count > 1 ? tr("Pages rotated")
                                                  : tr("Page rotated");
    case Kind::PageDeleted:    return e.count > 1 ? tr("Pages deleted")
                                                  : tr("Page deleted");
    case Kind::PageAdded:      return e.count > 1 ? tr("Pages added")
                                                  : tr("Page added");
    case Kind::PagesReordered: return tr("Pages reordered");
    case Kind::PagesOrganized: return tr("Pages organized");
    case Kind::Saved:          return tr("Document saved");
    case Kind::Reverted:       return tr("Went back to an earlier state");
    }
    return {};
}

QString HistoryDialog::detailFor(const DocumentHistory::Entry &e)
{
    using Kind = DocumentHistory::Kind;

    const QString page = e.page >= 0 ? tr("Page %1").arg(e.page + 1) : QString();
    const QString pages = tr("%1 pages").arg(e.count);

    switch (e.kind) {
    case Kind::Opened:
    case Kind::Saved:
        return e.text;
    case Kind::TextEdited:
    case Kind::TextRemoved:
    case Kind::ImageInserted:
    case Kind::ImageRemoved:
    case Kind::LinkAdded:
    case Kind::LinkEdited:
    case Kind::LinkRemoved:
    case Kind::NoteAdded:
    case Kind::NoteEdited:
    case Kind::NoteRemoved:
    case Kind::DrawingAdded:
    case Kind::DrawingRemoved:
        return page;
    case Kind::PageRotated: {
        const QString turn = e.value < 0 ? tr("%1° counter-clockwise").arg(-e.value)
                                         : tr("%1° clockwise").arg(e.value);
        if (e.count > 1) return pages + QStringLiteral(" · ") + turn;
        return page.isEmpty() ? turn : page + QStringLiteral(" · ") + turn;
    }
    case Kind::PageDeleted:
    case Kind::PageAdded:
        return e.count > 1 ? pages : page;
    case Kind::PagesReordered:
    case Kind::PagesOrganized:
        return pages;
    case Kind::Reverted:
        return tr("Step %1").arg(e.value);
    }
    return {};
}

QString HistoryDialog::iconFor(DocumentHistory::Kind kind)
{
    using Kind = DocumentHistory::Kind;
    switch (kind) {
    case Kind::Opened:         return QStringLiteral("file");
    case Kind::TextEdited:
    case Kind::TextRemoved:    return QStringLiteral("type");
    case Kind::ImageInserted:
    case Kind::ImageRemoved:   return QStringLiteral("image");
    case Kind::LinkAdded:
    case Kind::LinkEdited:     return QStringLiteral("paperclip");
    case Kind::LinkRemoved:    return QStringLiteral("trash-2");
    case Kind::NoteAdded:
    case Kind::NoteEdited:     return QStringLiteral("message-square");
    case Kind::NoteRemoved:    return QStringLiteral("trash-2");
    case Kind::DrawingAdded:   return QStringLiteral("pencil");
    case Kind::DrawingRemoved: return QStringLiteral("trash-2");
    case Kind::PageRotated:    return QStringLiteral("rotate-cw");
    case Kind::PageDeleted:    return QStringLiteral("trash-2");
    case Kind::PageAdded:      return QStringLiteral("file-plus");
    case Kind::PagesReordered: return QStringLiteral("arrow-left-right");
    case Kind::PagesOrganized: return QStringLiteral("layers");
    case Kind::Saved:          return QStringLiteral("save");
    case Kind::Reverted:       return QStringLiteral("refresh-cw");
    }
    return QStringLiteral("file");
}

void HistoryDialog::applyStyle()
{

    setStyleSheet(Theme::DarkMode ? QStringLiteral(R"(
QDialog { background: #2B2B2B; }
QLabel#HistoryHeadline { color: #EEEEEE; font-size: 17px; font-weight: 700; }
QLabel#HistorySubtitle { color: #9A9A9A; font-size: 13px; }
QScrollArea#HistoryScroll { background: #2B2B2B; border: none; }
QWidget#HistoryList { background: #2B2B2B; }
QScrollArea#HistoryScroll > QWidget > QWidget { background: #2B2B2B; }
QFrame#HistoryRow { background: transparent; border: none; border-radius: 10px; }
QFrame#HistoryRow:hover { background: #353535; }
QFrame#HistoryRow[selected="true"] { background: #1E3358; }
QLabel#HistoryMarker {
    background: #2B2B2B; border: 2px solid #4A4A4A; border-radius: 15px;
    color: #9A9A9A; font-size: 12px; font-weight: 700;
}
QLabel#HistoryMarker[selected="true"] {
    background: #2563EB; border-color: #2563EB; color: #FFFFFF;
}
QLabel#HistoryTime   { color: #9A9A9A; font-size: 12px; }
QLabel#HistoryTitle  { color: #EEEEEE; font-size: 14px; font-weight: 600; }
QLabel#HistoryDetail { color: #9A9A9A; font-size: 12px; }
QToolButton#HistoryMenuBtn { background: transparent; border: none; border-radius: 6px; }
QToolButton#HistoryMenuBtn:hover { background: #454545; }
QPushButton#HistoryBtn {
    background: #404040; border: 1px solid #505050; border-radius: 8px;
    color: #D8D8D8; font-size: 13px; padding: 8px 12px; icon-size: 16px;
}
QPushButton#HistoryBtn:hover  { background: #4A4A4A; border-color: #606060; }
QPushButton#HistoryBtn:pressed { background: #555555; }
QPushButton#HistoryBtn:disabled { color: #6B6B6B; background: #3A3A3A; border-color: #484848; }
QPushButton#HistoryClearBtn {
    background: transparent; border: none; border-radius: 8px;
    color: #F87171; font-size: 13px; padding: 8px 12px; icon-size: 16px;
}
QPushButton#HistoryClearBtn:hover { background: #4A2B2B; }
QPushButton#HistoryClearBtn:disabled { color: #7F4A4A; }
QPushButton#HistoryCloseBtn {
    background: #2563EB; border: none; border-radius: 8px;
    color: white; font-size: 13px; font-weight: 600; padding: 9px 26px;
}
QPushButton#HistoryCloseBtn:hover { background: #1D4ED8; }
QPushButton#HistoryCloseBtn:pressed { background: #1E40AF; }
QScrollArea#HistoryScroll QScrollBar:vertical {
    background: transparent; width: 10px; margin: 0;
}
QScrollArea#HistoryScroll QScrollBar::handle:vertical {
    background: #555555; border-radius: 5px; min-height: 32px;
}
QScrollArea#HistoryScroll QScrollBar::handle:vertical:hover { background: #666666; }
QScrollArea#HistoryScroll QScrollBar::add-line:vertical,
QScrollArea#HistoryScroll QScrollBar::sub-line:vertical { height: 0; }
)") : QStringLiteral(R"(
QDialog { background: #FFFFFF; }
QLabel#HistoryHeadline { color: #111827; font-size: 17px; font-weight: 700; }
QLabel#HistorySubtitle { color: #6B7280; font-size: 13px; }
QScrollArea#HistoryScroll { background: #FFFFFF; border: none; }
QWidget#HistoryList { background: #FFFFFF; }
QScrollArea#HistoryScroll > QWidget > QWidget { background: #FFFFFF; }
QFrame#HistoryRow { background: transparent; border: none; border-radius: 10px; }
QFrame#HistoryRow:hover { background: #F9FAFB; }
QFrame#HistoryRow[selected="true"] { background: #EFF6FF; }
QLabel#HistoryMarker {
    background: #FFFFFF; border: 2px solid #E5E7EB; border-radius: 15px;
    color: #6B7280; font-size: 12px; font-weight: 700;
}
QLabel#HistoryMarker[selected="true"] {
    background: #2563EB; border-color: #2563EB; color: #FFFFFF;
}
QLabel#HistoryTime   { color: #6B7280; font-size: 12px; }
QLabel#HistoryTitle  { color: #111827; font-size: 14px; font-weight: 600; }
QLabel#HistoryDetail { color: #6B7280; font-size: 12px; }
QToolButton#HistoryMenuBtn { background: transparent; border: none; border-radius: 6px; }
QToolButton#HistoryMenuBtn:hover { background: #F3F4F6; }
QPushButton#HistoryBtn {
    background: #FFFFFF; border: 1px solid #E5E7EB; border-radius: 8px;
    color: #374151; font-size: 13px; padding: 8px 12px; icon-size: 16px;
}
QPushButton#HistoryBtn:hover  { background: #F9FAFB; border-color: #CBD5E1; }
QPushButton#HistoryBtn:pressed { background: #F3F4F6; }
QPushButton#HistoryBtn:disabled { color: #9CA3AF; background: #F9FAFB; border-color: #F3F4F6; }
QPushButton#HistoryClearBtn {
    background: transparent; border: none; border-radius: 8px;
    color: #DC2626; font-size: 13px; padding: 8px 12px; icon-size: 16px;
}
QPushButton#HistoryClearBtn:hover { background: #FEF2F2; }
QPushButton#HistoryClearBtn:disabled { color: #FCA5A5; }
QPushButton#HistoryCloseBtn {
    background: #2563EB; border: none; border-radius: 8px;
    color: white; font-size: 13px; font-weight: 600; padding: 9px 26px;
}
QPushButton#HistoryCloseBtn:hover { background: #1D4ED8; }
QPushButton#HistoryCloseBtn:pressed { background: #1E40AF; }
QScrollArea#HistoryScroll QScrollBar:vertical {
    background: transparent; width: 10px; margin: 0;
}
QScrollArea#HistoryScroll QScrollBar::handle:vertical {
    background: #D1D5DB; border-radius: 5px; min-height: 32px;
}
QScrollArea#HistoryScroll QScrollBar::handle:vertical:hover { background: #9CA3AF; }
QScrollArea#HistoryScroll QScrollBar::add-line:vertical,
QScrollArea#HistoryScroll QScrollBar::sub-line:vertical { height: 0; }
)"));
}
