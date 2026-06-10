#pragma once

#include <QScrollArea>

class PagePlaceholder;

/// The central canvas area that displays page placeholders.
///
/// Inherits QScrollArea so the content scrolls freely.
/// Background: #F1F5F9.
/// Pages are stacked vertically with even spacing and centred horizontally.
class DocumentView : public QScrollArea
{
    Q_OBJECT

public:
    explicit DocumentView(QWidget *parent = nullptr);

    /// Replace the current pages with `count` new placeholder pages.
    void setPageCount(int count);

    void setZoom(int percent);
    [[nodiscard]] int zoom() const { return m_zoom; }

Q_SIGNALS:
    void zoomChanged(int percent);

private:
    void rebuildPages();

    int  m_pageCount { 3 };
    int  m_zoom      { 100 };

    QWidget *m_container { nullptr };
};
