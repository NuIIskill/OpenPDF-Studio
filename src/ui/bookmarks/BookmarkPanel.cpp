#include "ui/bookmarks/BookmarkPanel.hpp"

#include "ui/theme/Theme.hpp"
#include "ui/widgets/IconButton.hpp"

#include <QAction>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QVariant pathVariant(const QList<int> &path)
{
    QVariantList values;
    values.reserve(path.size());
    for (int index : path) values.append(index);
    return values;
}

QList<int> variantPath(const QVariant &variant)
{
    QList<int> path;
    const QVariantList values = variant.toList();
    path.reserve(values.size());
    for (const QVariant &value : values) path.append(value.toInt());
    return path;
}

}

BookmarkPanel::BookmarkPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("BookmarkPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("BookmarkPanelTitle"));
    header->addWidget(m_title, 1);
    m_close = new IconButton(this);
    m_close->setObjectName(QStringLiteral("BookmarkPanelClose"));
    m_close->setFixedSize(28, 28);
    m_close->setIconSize(QSize(17, 17));
    connect(m_close, &QPushButton::clicked,
            this, &BookmarkPanel::closeRequested);
    header->addWidget(m_close);
    layout->addLayout(header);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("BookmarkSearch"));
    m_search->setFixedHeight(34);
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);
    connect(m_search, &QLineEdit::textChanged,
            this, &BookmarkPanel::filterTree);

    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(2, 0, 2, 0);
    actions->setSpacing(12);
    m_add      = new IconButton(this);
    m_rename   = new IconButton(this);
    m_delete   = new IconButton(this);
    m_moveUp   = new IconButton(this);
    m_moveDown = new IconButton(this);
    for (IconButton *button : { m_add, m_rename, m_delete,
                                m_moveUp, m_moveDown }) {
        button->setObjectName(QStringLiteral("BookmarkAction"));
        button->setFixedSize(28, 28);
        button->setIconSize(QSize(17, 17));
        actions->addWidget(button);
    }
    actions->addStretch(1);
    layout->addLayout(actions);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("BookmarkTree"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(16);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    m_empty = new QLabel(this);
    m_empty->setObjectName(QStringLiteral("BookmarkEmpty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);

    m_content = new QStackedWidget(this);
    m_content->setObjectName(QStringLiteral("BookmarkContent"));
    m_content->addWidget(m_tree);
    m_content->addWidget(m_empty);
    layout->addWidget(m_content, 1);

    connect(m_tree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item) {
        const int page = item ? item->data(0, Qt::UserRole).toInt() : -1;
        if (page >= 0 && page < m_pageCount) Q_EMIT pageRequested(page);
    });
    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &BookmarkPanel::updateActions);
    connect(m_tree, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) { setExpanded(item, true); });
    connect(m_tree, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem *item) { setExpanded(item, false); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *) {
        if (m_editingEnabled) renameBookmark();
    });

    connect(m_add, &QPushButton::clicked, this, &BookmarkPanel::addBookmark);
    connect(m_rename, &QPushButton::clicked,
            this, &BookmarkPanel::renameBookmark);
    connect(m_delete, &QPushButton::clicked,
            this, &BookmarkPanel::deleteBookmark);
    connect(m_moveUp, &QPushButton::clicked,
            this, [this]() { moveBookmark(-1); });
    connect(m_moveDown, &QPushButton::clicked,
            this, [this]() { moveBookmark(1); });

    retranslateUi();
    refreshTheme();
    rebuildTree();
}

void BookmarkPanel::setDocument(const QList<PdfBookmark> &bookmarks,
                                int pageCount, bool editingEnabled)
{
    m_bookmarks       = bookmarks;
    m_pageCount       = qMax(0, pageCount);
    m_editingEnabled  = editingEnabled;
    m_currentPage     = qBound(0, m_currentPage, qMax(0, m_pageCount - 1));
    rebuildTree();
}

void BookmarkPanel::setCurrentPage(int page)
{
    m_currentPage = qBound(0, page, qMax(0, m_pageCount - 1));
}

void BookmarkPanel::refreshTheme()
{
    m_close->setIconName(QStringLiteral("x"), Theme::IconMuted);
    m_add->setIconName(QStringLiteral("plus"), Theme::IconMuted);
    m_rename->setIconName(QStringLiteral("pencil"), Theme::IconMuted);
    m_delete->setIconName(QStringLiteral("trash-2"), Theme::IconMuted);
    m_moveUp->setIconName(QStringLiteral("chevron-up"), Theme::IconMuted);
    m_moveDown->setIconName(QStringLiteral("chevron-down"), Theme::IconMuted);
    const QIcon search = Theme::makeIcon(QStringLiteral("search"), Theme::IconMuted);
    const QList<QAction *> actions = m_search->actions();
    if (actions.isEmpty()) m_search->addAction(search, QLineEdit::LeadingPosition);
    else                   actions.first()->setIcon(search);
}

void BookmarkPanel::retranslateUi()
{
    m_title->setText(tr("Bookmarks"));
    m_search->setPlaceholderText(tr("Search bookmarks"));
    m_empty->setText(m_pageCount > 0
        ? tr("This document has no bookmarks.")
        : tr("Open a PDF to view its bookmarks."));
    m_close->setToolTip(tr("Close bookmarks"));
    m_add->setToolTip(tr("Add bookmark for current page"));
    m_rename->setToolTip(tr("Rename bookmark"));
    m_delete->setToolTip(tr("Delete bookmark"));
    m_moveUp->setToolTip(tr("Move bookmark up"));
    m_moveDown->setToolTip(tr("Move bookmark down"));
}

void BookmarkPanel::rebuildTree(const Path &selection)
{
    m_rebuilding = true;
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();
    appendItems(nullptr, m_bookmarks, {});
    if (QTreeWidgetItem *item = itemAt(selection)) {
        m_tree->setCurrentItem(item);
        item->setSelected(true);
    }
    m_rebuilding = false;

    filterTree(m_search->text());
    m_empty->setText(m_pageCount > 0
        ? tr("This document has no bookmarks.")
        : tr("Open a PDF to view its bookmarks."));
    if (m_bookmarks.isEmpty()) m_content->setCurrentWidget(m_empty);
    else                       m_content->setCurrentWidget(m_tree);
    updateActions();
}

void BookmarkPanel::appendItems(QTreeWidgetItem *parent,
                                const QList<PdfBookmark> &items,
                                const Path &parentPath)
{
    for (int i = 0; i < items.size(); ++i) {
        const PdfBookmark &bookmark = items[i];
        auto *item = parent ? new QTreeWidgetItem(parent)
                            : new QTreeWidgetItem(m_tree);
        Path path = parentPath;
        path.append(i);
        item->setText(0, bookmark.title);
        item->setText(1, bookmark.page >= 0 ? QString::number(bookmark.page + 1)
                                             : QString{});
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, Qt::UserRole, bookmark.page);
        item->setData(0, Qt::UserRole + 1, pathVariant(path));
        appendItems(item, bookmark.children, path);
        item->setExpanded(bookmark.expanded);
    }
}

void BookmarkPanel::filterTree(const QString &text)
{
    const QString needle = text.trimmed();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        filterItem(m_tree->topLevelItem(i), needle);
}

bool BookmarkPanel::filterItem(QTreeWidgetItem *item, const QString &needle)
{
    bool childMatches = false;
    for (int i = 0; i < item->childCount(); ++i)
        childMatches = filterItem(item->child(i), needle) || childMatches;
    const bool matches = needle.isEmpty()
        || item->text(0).contains(needle, Qt::CaseInsensitive)
        || childMatches;
    item->setHidden(!matches);
    if (!needle.isEmpty() && childMatches) item->setExpanded(true);
    return matches;
}

BookmarkPanel::Path BookmarkPanel::itemPath(const QTreeWidgetItem *item) const
{
    return item ? variantPath(item->data(0, Qt::UserRole + 1)) : Path{};
}

QTreeWidgetItem *BookmarkPanel::itemAt(const Path &path) const
{
    if (path.isEmpty()) return nullptr;
    QTreeWidgetItem *item = nullptr;
    for (int depth = 0; depth < path.size(); ++depth) {
        const int index = path[depth];
        item = depth == 0 ? m_tree->topLevelItem(index)
                          : (item ? item->child(index) : nullptr);
        if (!item) return nullptr;
    }
    return item;
}

QList<PdfBookmark> *BookmarkPanel::siblingsAt(const Path &path)
{
    QList<PdfBookmark> *siblings = &m_bookmarks;
    for (int depth = 0; depth + 1 < path.size(); ++depth) {
        const int index = path[depth];
        if (index < 0 || index >= siblings->size()) return nullptr;
        siblings = &(*siblings)[index].children;
    }
    return siblings;
}

PdfBookmark *BookmarkPanel::bookmarkAt(const Path &path)
{
    QList<PdfBookmark> *siblings = siblingsAt(path);
    if (!siblings || path.isEmpty()) return nullptr;
    const int index = path.last();
    return index >= 0 && index < siblings->size() ? &(*siblings)[index] : nullptr;
}

void BookmarkPanel::updateActions()
{
    const Path path = itemPath(m_tree->currentItem());
    QList<PdfBookmark> *siblings = siblingsAt(path);
    const bool selected = !path.isEmpty() && siblings;
    const int index = selected ? path.last() : -1;

    m_add->setEnabled(m_editingEnabled && m_pageCount > 0);
    m_rename->setEnabled(m_editingEnabled && selected);
    m_delete->setEnabled(m_editingEnabled && selected);
    m_moveUp->setEnabled(m_editingEnabled && selected && index > 0);
    m_moveDown->setEnabled(m_editingEnabled && selected
                           && index + 1 < siblings->size());
}

void BookmarkPanel::addBookmark()
{
    if (!m_editingEnabled || m_pageCount <= 0) return;
    bool ok = false;
    const QString title = QInputDialog::getText(
        this, tr("Add bookmark"), tr("Title:"), QLineEdit::Normal,
        tr("Page %1").arg(m_currentPage + 1), &ok).trimmed();
    if (!ok || title.isEmpty()) return;

    Path path = itemPath(m_tree->currentItem());
    QList<PdfBookmark> *siblings = path.isEmpty() ? &m_bookmarks : siblingsAt(path);
    int index = path.isEmpty() ? siblings->size() : path.last() + 1;
    siblings->insert(index, PdfBookmark{ title, m_currentPage, true, {} });
    if (path.isEmpty()) path.append(index);
    else                path.last() = index;
    rebuildTree(path);
    Q_EMIT bookmarksEdited(m_bookmarks);
}

void BookmarkPanel::renameBookmark()
{
    if (!m_editingEnabled) return;
    const Path path = itemPath(m_tree->currentItem());
    PdfBookmark *bookmark = bookmarkAt(path);
    if (!bookmark) return;

    bool ok = false;
    const QString title = QInputDialog::getText(
        this, tr("Rename bookmark"), tr("Title:"), QLineEdit::Normal,
        bookmark->title, &ok).trimmed();
    if (!ok || title.isEmpty() || title == bookmark->title) return;
    bookmark->title = title;
    rebuildTree(path);
    Q_EMIT bookmarksEdited(m_bookmarks);
}

void BookmarkPanel::deleteBookmark()
{
    if (!m_editingEnabled) return;
    const Path path = itemPath(m_tree->currentItem());
    QList<PdfBookmark> *siblings = siblingsAt(path);
    if (!siblings || path.isEmpty()) return;
    const int index = path.last();
    if (index < 0 || index >= siblings->size()) return;

    const auto answer = QMessageBox::question(
        this, tr("Delete bookmark"),
        tr("Delete \"%1\" and its child bookmarks?").arg((*siblings)[index].title),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

    siblings->removeAt(index);
    Path next = path;
    if (siblings->isEmpty()) next.clear();
    else next.last() = qMin(index, siblings->size() - 1);
    rebuildTree(next);
    Q_EMIT bookmarksEdited(m_bookmarks);
}

void BookmarkPanel::moveBookmark(int delta)
{
    if (!m_editingEnabled) return;
    Path path = itemPath(m_tree->currentItem());
    QList<PdfBookmark> *siblings = siblingsAt(path);
    if (!siblings || path.isEmpty()) return;
    const int from = path.last();
    const int to = from + delta;
    if (from < 0 || from >= siblings->size() || to < 0 || to >= siblings->size())
        return;
    siblings->swapItemsAt(from, to);
    path.last() = to;
    rebuildTree(path);
    Q_EMIT bookmarksEdited(m_bookmarks);
}

void BookmarkPanel::setExpanded(QTreeWidgetItem *item, bool expanded)
{
    if (m_rebuilding) return;
    if (PdfBookmark *bookmark = bookmarkAt(itemPath(item)))
        bookmark->expanded = expanded;
}
