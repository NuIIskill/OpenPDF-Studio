#pragma once

#include <QWidget>

/// A single A4 page rendered as a white card with mock content.
///
/// Dimensions: 595×842 px (full 1:1 A4 at 72 dpi).
/// A subtle box-shadow is drawn via QPainter in paintEvent().
class PagePlaceholder : public QWidget
{
    Q_OBJECT

public:
    explicit PagePlaceholder(int pageNumber, QWidget *parent = nullptr);

    [[nodiscard]] int pageNumber() const { return m_pageNumber; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_pageNumber { 1 };
};
