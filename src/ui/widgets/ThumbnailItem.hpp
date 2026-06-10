#pragma once

#include <QWidget>

/// A single page thumbnail entry inside the left sidebar scroll area.
///
/// Renders a miniature page preview (white rounded rect) and a page
/// number label underneath.  Selected state is indicated by a 3 px
/// blue left-edge bar painted in paintEvent().
class ThumbnailItem : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected NOTIFY selectionChanged)

public:
    explicit ThumbnailItem(int pageNumber, QWidget *parent = nullptr);

    [[nodiscard]] int  pageNumber() const { return m_pageNumber; }
    [[nodiscard]] bool isSelected() const { return m_selected; }
    void setSelected(bool selected);

Q_SIGNALS:
    void selectionChanged(bool selected);
    void clicked(int pageNumber);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int  m_pageNumber { 1 };
    bool m_selected   { false };
    bool m_hovered    { false };
};
