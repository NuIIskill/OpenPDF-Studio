#include "ui/tools/CommentAnnotation.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QMouseEvent>

CommentAnnotation::CommentAnnotation(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("CommentAnnotation"));
    setFixedSize(WIDTH, EXPANDED_H);
    setStyleSheet(QStringLiteral(
        "CommentAnnotation { background:#FFF7ED; border:1px solid #F97316; border-radius:4px; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *bar = new QWidget(this);
    bar->setFixedHeight(BAR_H);
    bar->setStyleSheet(QStringLiteral("background:#FB923C; border-radius:3px 3px 0 0;"));
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(6, 0, 4, 0);
    barLayout->setSpacing(2);

    auto *icon = new QLabel(QStringLiteral("💬"), bar);
    icon->setFixedWidth(16);
    icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    barLayout->addWidget(icon);

    auto *titleLbl = new QLabel(tr("Comment"), bar);
    titleLbl->setStyleSheet(QStringLiteral("font-size:11px; font-weight:600; color:#7C2D12;"));
    titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    barLayout->addWidget(titleLbl);
    barLayout->addStretch();

    m_toggleBtn = new QPushButton(QStringLiteral("▾"), bar);
    m_toggleBtn->setFixedSize(16, 16);
    m_toggleBtn->setFlat(true);
    m_toggleBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size:10px; color:#7C2D12; border:none; background:transparent; }"
        "QPushButton:hover { color:#000; }"));
    m_toggleBtn->setCursor(Qt::PointingHandCursor);
    connect(m_toggleBtn, &QPushButton::clicked, this, &CommentAnnotation::toggle);
    barLayout->addWidget(m_toggleBtn);

    auto *closeBtn = new QPushButton(QStringLiteral("✕"), bar);
    closeBtn->setFixedSize(16, 16);
    closeBtn->setFlat(true);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size:10px; color:#7C2D12; border:none; background:transparent; }"
        "QPushButton:hover { color:#000; }"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &CommentAnnotation::deleteRequested);
    barLayout->addWidget(closeBtn);

    root->addWidget(bar);

    m_edit = new QTextEdit(this);
    m_edit->setPlaceholderText(tr("Enter comment…"));
    m_edit->setStyleSheet(QStringLiteral(
        "QTextEdit { background:transparent; border:none; font-size:12px; }"));
    root->addWidget(m_edit, 1);
}

void CommentAnnotation::toggle()
{
    m_expanded = !m_expanded;
    m_edit->setVisible(m_expanded);
    setFixedHeight(m_expanded ? EXPANDED_H : COLLAPSED_H);
    m_toggleBtn->setText(m_expanded ? QStringLiteral("▾") : QStringLiteral("▸"));
}

void CommentAnnotation::mousePressEvent(QMouseEvent *e)
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

void CommentAnnotation::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) {
        move(mapToParent(e->pos()) - m_dragOff);
        e->accept();
        return;
    }
    QFrame::mouseMoveEvent(e);
}

void CommentAnnotation::mouseReleaseEvent(QMouseEvent *e)
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
