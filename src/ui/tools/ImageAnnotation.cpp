#include "ImageAnnotation.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QPixmap>

ImageAnnotation::ImageAnnotation(const QString &imagePath, QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("ImageAnnotation"));
    setStyleSheet(QStringLiteral(
        "ImageAnnotation { background:#F0FDF4; border:1px solid #22C55E; border-radius:4px; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Drag bar
    auto *bar = new QWidget(this);
    bar->setFixedHeight(BAR_H);
    bar->setStyleSheet(QStringLiteral("background:#4ADE80; border-radius:3px 3px 0 0;"));
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(6, 0, 4, 0);
    barLayout->setSpacing(0);

    auto *titleLbl = new QLabel(tr("Image"), bar);
    titleLbl->setStyleSheet(QStringLiteral("font-size:11px; font-weight:600; color:#14532D;"));
    titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    barLayout->addWidget(titleLbl);
    barLayout->addStretch();

    auto *closeBtn = new QPushButton(QStringLiteral("✕"), bar);
    closeBtn->setFixedSize(16, 16);
    closeBtn->setFlat(true);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size:10px; color:#14532D; border:none; background:transparent; }"
        "QPushButton:hover { color:#000; }"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &ImageAnnotation::deleteRequested);
    barLayout->addWidget(closeBtn);

    root->addWidget(bar);

    // Image label
    m_imgLabel = new QLabel(this);
    m_imgLabel->setAlignment(Qt::AlignCenter);

    QPixmap px(imagePath);
    if (!px.isNull()) {
        if (px.width() > MAX_W || px.height() > MAX_H)
            px = px.scaled(MAX_W, MAX_H, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_imgLabel->setPixmap(px);
        setFixedSize(px.width(), BAR_H + px.height());
    } else {
        m_imgLabel->setText(tr("(image not found)"));
        setFixedSize(200, BAR_H + 40);
    }

    root->addWidget(m_imgLabel);
}

void ImageAnnotation::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && e->pos().y() <= BAR_H) {
        m_posBeforeDrag = pos();
        m_dragOff       = e->pos();
        m_dragging      = true;
        e->accept();
        return;
    }
    QFrame::mousePressEvent(e);
}

void ImageAnnotation::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) {
        move(mapToParent(e->pos()) - m_dragOff);
        e->accept();
        return;
    }
    QFrame::mouseMoveEvent(e);
}

void ImageAnnotation::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        if (pos() != m_posBeforeDrag)
            Q_EMIT moved(m_posBeforeDrag, pos());
        e->accept();
        return;
    }
    QFrame::mouseReleaseEvent(e);
}
