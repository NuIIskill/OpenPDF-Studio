#pragma once

#include <QWidget>
#include <QList>

QT_BEGIN_NAMESPACE
class QScrollArea;
class QVBoxLayout;
QT_END_NAMESPACE

class ThumbnailItem;

/// The dark left sidebar showing page thumbnails.
///
/// Fixed width: 220 px.
/// Consists of a header ("Seiten") and a scroll area containing
/// ThumbnailItem widgets, one per document page.
class LeftSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit LeftSidebar(QWidget *parent = nullptr);

    /// Replace all thumbnails with `count` placeholder pages.
    void setPageCount(int count);

    /// Mark the thumbnail at index `page` (1-based) as selected.
    void setCurrentPage(int page);

Q_SIGNALS:
    void pageClicked(int pageNumber);

private:
    void clearThumbnails();

    QScrollArea   *m_scrollArea     { nullptr };
    QVBoxLayout   *m_thumbnailLayout{ nullptr };
    QList<ThumbnailItem *> m_items;
};
