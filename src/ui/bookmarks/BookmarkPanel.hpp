#pragma once

#include "engine/document/PdfBookmark.hpp"

#include <QList>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

class IconButton;

/// Searchable document-outline panel opened by the bookmark tool.
class BookmarkPanel : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kWidth = 300;

    explicit BookmarkPanel(QWidget *parent = nullptr);

    void setDocument(const QList<PdfBookmark> &bookmarks, int pageCount,
                     bool editingEnabled);
    void setCurrentPage(int page);
    void refreshTheme();
    void retranslateUi();

Q_SIGNALS:
    void closeRequested();
    void pageRequested(int page);
    void bookmarksEdited(const QList<PdfBookmark> &bookmarks);

private:
    using Path = QList<int>;

    void rebuildTree(const Path &selection = {});
    void appendItems(QTreeWidgetItem *parent, const QList<PdfBookmark> &items,
                     const Path &parentPath);
    void filterTree(const QString &text);
    bool filterItem(QTreeWidgetItem *item, const QString &needle);
    Path itemPath(const QTreeWidgetItem *item) const;
    QTreeWidgetItem *itemAt(const Path &path) const;
    QList<PdfBookmark> *siblingsAt(const Path &path);
    PdfBookmark *bookmarkAt(const Path &path);
    void updateActions();
    void addBookmark();
    void renameBookmark();
    void deleteBookmark();
    void moveBookmark(int delta);
    void setExpanded(QTreeWidgetItem *item, bool expanded);

    QLabel         *m_title       { nullptr };
    QLineEdit      *m_search      { nullptr };
    QTreeWidget    *m_tree        { nullptr };
    QStackedWidget *m_content     { nullptr };
    QLabel         *m_empty       { nullptr };
    IconButton     *m_close       { nullptr };
    IconButton     *m_add         { nullptr };
    IconButton     *m_rename      { nullptr };
    IconButton     *m_delete      { nullptr };
    IconButton     *m_moveUp      { nullptr };
    IconButton     *m_moveDown    { nullptr };

    QList<PdfBookmark> m_bookmarks;
    int                m_pageCount { 0 };
    int                m_currentPage { 0 };
    bool               m_editingEnabled { false };
    bool               m_rebuilding { false };
};
