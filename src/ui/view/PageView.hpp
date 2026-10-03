#pragma once

#include <QLabel>
#include <QPixmap>

/// Shows one page: its frame and its render, drawn as it is without QLabel rescaling it on every paint.
class PageView : public QLabel
{
public:
    explicit PageView(QWidget *parent = nullptr);

    void    setPicture(const QPixmap &picture);
    QPixmap picture() const { return m_picture; }
    bool    hasPicture() const { return !m_picture.isNull(); }

    static void setPicture(QLabel *label, const QPixmap &picture);
    static bool hasPicture(const QLabel *label);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    QPixmap m_picture;
};
