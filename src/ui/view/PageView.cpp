#include "ui/view/PageView.hpp"

#include <QPaintEvent>
#include <QPainter>
#include <QStyle>

PageView::PageView(QWidget *parent)
    : QLabel(parent)
{
}

void PageView::setPicture(const QPixmap &picture)
{
    m_picture = picture;
    update();
}

void PageView::setPicture(QLabel *label, const QPixmap &picture)
{
    if (auto *view = dynamic_cast<PageView *>(label)) view->setPicture(picture);
    else if (label) label->setPixmap(picture);
}

bool PageView::hasPicture(const QLabel *label)
{
    if (const auto *view = dynamic_cast<const PageView *>(label)) return view->hasPicture();
    return label && !label->pixmap().isNull();
}

void PageView::paintEvent(QPaintEvent *e)
{
    // The frame as QLabel draws it, then only the part of the picture that
    // needs painting, placed as QLabel would place it.
    QFrame::paintEvent(e);
    if (m_picture.isNull()) return;
    QPainter painter(this);
    painter.setClipRect(e->rect() & contentsRect());
    const QSizeF logical = QSizeF(m_picture.size()) / m_picture.devicePixelRatio();
    const QRect target = QStyle::alignedRect(layoutDirection(), Qt::AlignCenter,
                                             logical.toSize(), contentsRect());
    painter.drawPixmap(QRectF(target.topLeft(), logical), m_picture, QRectF(m_picture.rect()));
}
